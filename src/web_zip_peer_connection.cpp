/*

Copyright (c) 2006-2007, 2009-2020, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/


#include "libtorrent/web_zip_peer_connection.hpp"
#include "libtorrent/miniz.hpp"
using namespace libtorrent;

void web_zip_peer_connection::send_block_requests()
{
	TORRENT_ASSERT(is_single_thread());
	INVARIANT_CHECK;

	std::shared_ptr<torrent> t = m_torrent.lock();
	TORRENT_ASSERT(t);

	if (m_disconnecting) return;

	// TODO: 3 once peers are properly put in graceful pause mode, they can
	// cancel all outstanding requests and this test can be removed.
	if (t->graceful_pause()) return;

	// we can't download pieces in these states
	if (t->state() == torrent_status::checking_files
		|| t->state() == torrent_status::checking_resume_data
		|| t->state() == torrent_status::downloading_metadata)
		return;

	if (int(m_download_queue.size()) >= m_desired_queue_size
		|| t->upload_mode()) return;

	if (m_request_queue.empty()) return;

	bool const empty_download_queue = m_download_queue.empty();

	while (!m_request_queue.empty() && (int(m_download_queue.size()) < m_desired_queue_size
			|| m_queued_time_critical > 0))
	{
		pending_block block = m_request_queue.front();
		auto iter_r = std::find_if(m_requests.begin(), m_requests.end(), [index = block.block.piece_index](const peer_request& pr) {
			return pr.piece == index;
		});
		if (iter_r != m_requests.end()) continue; // every piece only need request once.
			
		auto iter = std::remove_if(m_request_queue.begin(), m_request_queue.end(),
			[index = block.block.piece_index](const pending_block& block) {
			return block.block.piece_index == index;
		});
		m_request_queue.erase(iter, m_request_queue.end());
		if (m_queued_time_critical) --m_queued_time_critical;

		// if we're a seed, we don't have a piece picker
		// so we don't have to worry about invariants getting
		// out of sync with it
		if (!t->has_picker()) continue;

		// this can happen if a block times out, is re-requested and
		// then arrives "unexpectedly"
		if (t->picker().is_downloaded(block.block))
		{
			t->picker().abort_download(block.block, peer_info_struct());
			continue;
		}


		peer_request r;
		r.piece = block.block.piece_index;
		r.start = 0;
		r.length = t->torrent_file().piece_size(block.block.piece_index);

		if (m_download_queue.empty())
			m_counters.inc_stats_counter(counters::num_peers_down_requests);

		piece_block pb(block.block);
		pb.block_index = 0;
		do 
		{
			if (r.length - pb.block_index * t->block_size() <= 0) break;
			m_download_queue.push_back(pb);
			pb.block_index += 1;
		} while (true);
		//TORRENT_ASSERT(verify_piece(t->to_req(block.block)));
		//m_download_queue.push_back(block);
		m_outstanding_bytes += r.length;

		
		// the verification will fail for coalesced blocks
		TORRENT_ASSERT(verify_piece(r) || m_request_large_blocks);

#ifndef TORRENT_DISABLE_EXTENSIONS
		bool handled = false;
		for (auto const& e : m_extensions)
		{
			handled = e->write_request(r);
			if (handled) break;
		}
		if (is_disconnecting()) return;
		if (!handled)
#endif
		{
			write_request(r);
			m_last_request.set(m_connect, aux::time_now());
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::outgoing_message))
		{
			peer_log(peer_log_alert::outgoing_message, "REQUEST"
				, "piece: %d s: %x l: %x ds: %dB/s dqs: %d rqs: %d blk: %s"
				, static_cast<int>(r.piece), r.start, r.length, statistics().download_rate()
				, int(m_desired_queue_size), int(m_download_queue.size())
				, m_request_large_blocks ? "large" : "single");
		}
#endif
	}
		
		
	m_last_piece.set(m_connect, aux::time_now());

	if (empty_download_queue)
	{
		// This means we just added a request to this connection that
		// previously did not have a request. That's when we start the
		// request timeout.
		m_requested.set(m_connect, aux::time_now());
	}
}

void web_zip_peer_connection::write_request(peer_request const& r) {
	INVARIANT_CHECK;

	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	TORRENT_ASSERT(t->valid_metadata());

	torrent_info const& info = t->torrent_file();

	std::string request;
	request.reserve(400);

	//const int block_size = t->block_size();
	//const int piece_size = t->torrent_file().piece_length();


#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::outgoing_message, "REQUESTING", "(piece: %d start: %d) - (piece: %d end: %d)"
		, static_cast<int>(r.piece), r.start
		, static_cast<int>(r.piece), r.start + r.length);
#endif

	int const proxy_type = m_settings.get_int(settings_pack::proxy_type);
	bool const using_proxy = (proxy_type == settings_pack::http
		|| proxy_type == settings_pack::http_pw) && !m_ssl;

	// the number of pad files that have been "requested". In case we _only_
	// request padfiles, we can't rely on handling them in the on_receive()
	// callback (because we won't receive anything), instead we have to post a
	// pretend read callback where we can deliver the zeroes for the partfile
	// int num_pad_files = 0;

	// TODO: 3 do we really need a special case here? wouldn't the multi-file
	// case handle single file torrents correctly too?
	TORRENT_ASSERT(r.piece < piece_index_t(info.zip_web_seeds().pieces_size.size()));
	const auto& zip_piece = info.zip_web_seeds().pieces_size[int32_t(r.piece)];

	peer_request pr{};
	pr.piece = r.piece;
	pr.length = zip_piece.size;
	pr.start = r.start;
	pr.unzip_length = r.length;
	m_requests.push_back(pr);

	request += "GET ";
	// do not encode single file paths, they are
	// assumed to be encoded in the torrent file
	request += using_proxy ? m_url : m_path;
	request += " HTTP/1.1\r\n";
	add_headers(request, m_settings, using_proxy);
	request += "\r\nRange: bytes=";
	request += to_string(zip_piece.offset).data();
	request += "-";
	request += to_string(zip_piece.offset + zip_piece.size - 1).data();
	request += "\r\n\r\n";

	file_request_t file_req;
	file_req.file_index = file_index_t(0);
	file_req.start = zip_piece.offset;
	file_req.length = zip_piece.size;

	m_file_requests.push_back(file_req);

	//std::vector<file_slice> files = info.orig_files().map_block(r.piece, 0, info.piece_length());
	//for (auto&& f : files)
	//{
	//	

	//	if (info.orig_files().pad_file_at(f.file_index))
	//	{
	//		++num_pad_files;
	//	}
	//}

	//if (num_pad_files == int(m_file_requests.size()))
	//{
	//	post(get_context(), std::bind(
	//		&web_peer_connection::on_receive_padfile,
	//		std::static_pointer_cast<web_peer_connection>(self())));
	//	return;
	//}

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::outgoing_message, "REQUEST", "%s", request.c_str());
#endif

	send_buffer(request);
}

void web_zip_peer_connection::incoming_piece_fragment(int const bytes)
{
	TORRENT_ASSERT(is_single_thread());
	m_last_piece.set(m_connect, aux::time_now());
	TORRENT_ASSERT_VAL(m_outstanding_bytes >= bytes, m_outstanding_bytes - bytes);
	m_outstanding_bytes -= bytes;
	if (m_outstanding_bytes < 0) m_outstanding_bytes = 0;
	std::shared_ptr<torrent> t = associated_torrent().lock();
#if TORRENT_USE_ASSERTS
	// TORRENT_ASSERT(m_received_in_piece + bytes <= t->block_size());
	m_received_in_piece += bytes;
#endif

	// progress of this torrent increased
	t->state_updated();

#if TORRENT_USE_INVARIANT_CHECKS
	check_invariant();
#endif
}

void web_zip_peer_connection::incoming_payload(char const* buf, int len)
{
	received_bytes(len, 0);
	m_received_body += len;

	if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::incoming_message, "INCOMING_PAYLOAD", "%d bytes", len);
#endif

	// deliver all complete bittorrent requests to the bittorrent engine
	while (len > 0)
	{
		if (m_requests.empty()) return;

		TORRENT_ASSERT(!m_requests.empty());
		peer_request const& front_request = m_requests.front();
		int const piece_size = int(m_piece.size());
		int const copy_size = std::min(front_request.length - piece_size, len);

		// m_piece may not hold more than the response to the next BT request
		TORRENT_ASSERT(front_request.length > piece_size);

		// copy_size is the number of bytes we need to add to the end of m_piece
		// to not exceed the size of the next bittorrent request to be delivered.
		// m_piece can only hold the response for a single BT request at a time
		m_piece.resize(piece_size + copy_size);
		std::memcpy(m_piece.data() + piece_size, buf, aux::numeric_cast<std::size_t>(copy_size));
		len -= copy_size;
		buf += copy_size;

		// keep peer stats up-to-date
		incoming_piece_fragment(copy_size);

		TORRENT_ASSERT(front_request.length >= piece_size);
		if (int(m_piece.size()) == front_request.length)
		{
			std::shared_ptr<torrent> t = associated_torrent().lock();
			TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "POP_REQUEST"
				, "piece: %d start: %d len: %d"
				, static_cast<int>(front_request.piece), front_request.start, front_request.length);
#endif

			// Make a copy of the request and pop it off the queue before calling
			// incoming_piece because that may lead to a call to disconnect()
			// which will clear the request queue and invalidate any references
			// to the request
			peer_request front_request_copy = front_request;
			m_requests.pop_front();
			
			std::string unzip_buf;
			mz_ulong unzip_len = front_request_copy.unzip_length;
			unzip_buf.resize(unzip_len);
			auto ret = mz_uncompress((unsigned char*)unzip_buf.data(), &unzip_len, (const unsigned char*)m_piece.data(), m_piece.size());
			if (ret != MZ_OK) {
				m_piece.clear();
				return;
			}
			m_piece.clear();


			peer_request ipr(front_request_copy);
			ipr.start = 0;
			ipr.length = 0;
			while (unzip_len > 0)
			{
				ipr.length = unzip_len >= (mz_ulong)t->block_size() ? t->block_size() : unzip_len;
				unzip_len -= ipr.length;
#if TORRENT_USE_ASSERTS
				m_received_in_piece = ipr.length;
#endif
				incoming_piece(ipr, unzip_buf.data() + ipr.start);
				ipr.start += ipr.length;
			}
		}
	}
}
