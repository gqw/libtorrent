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

#ifndef TORRENT_WEB_ZIP_PEER_CONNECTION_HPP_INCLUDED
#define TORRENT_WEB_ZIP_PEER_CONNECTION_HPP_INCLUDED


#include "libtorrent/web_peer_connection.hpp"

namespace libtorrent {

	class TORRENT_EXTRA_EXPORT web_zip_peer_connection
		: public web_peer_connection
	{
	public:

		// this is the constructor where the we are the active part.
		// The peer_connection should handshake and verify that the
		// other end has the correct id
		web_zip_peer_connection(peer_connection_args& pack
			, web_seed_t& web) :web_peer_connection(pack, web){
			m_picker_options |= piece_picker::align_expanded_pieces;
			m_picker_options |= piece_picker::on_parole;
		}

		// void on_connected() override;

		//connection_type type() const override
		//{ return connection_type::url_zip_seed; }

	protected:
		void send_block_requests() override;
		void incoming_payload(char const* buf, int len) override;
		void write_request(peer_request const& r) override;
		void incoming_piece_fragment(int const bytes);

	};
}

#endif // TORRENT_WEB_ZIP_PEER_CONNECTION_HPP_INCLUDED
