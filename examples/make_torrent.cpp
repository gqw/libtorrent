/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2004, 2008-2013, 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2017, Steven Siloti
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

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/miniz.hpp"
#include "libtorrent/hex.hpp"


#include <functional>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _getcwd
#endif

namespace {

using namespace std::placeholders;
namespace fs = std::experimental::filesystem;

std::vector<char> load_file(std::string const& filename)
{
	std::fstream in;
	in.exceptions(std::ifstream::failbit);
	in.open(filename.c_str(), std::ios_base::in | std::ios_base::binary);
	in.seekg(0, std::ios_base::end);
	size_t const size = size_t(in.tellg());
	in.seekg(0, std::ios_base::beg);
	std::vector<char> ret(size);
	in.read(ret.data(), int(ret.size()));
	return ret;
}

std::string branch_path(std::string const& f)
{
	if (f.empty()) return f;

#ifdef TORRENT_WINDOWS
	if (f == "\\\\") return "";
#endif
	if (f == "/") return "";

	auto len = f.size();
	// if the last character is / or \ ignore it
	if (f[len-1] == '/' || f[len-1] == '\\') --len;
	while (len > 0) {
		--len;
		if (f[len] == '/' || f[len] == '\\')
			break;
	}

	if (f[len] == '/' || f[len] == '\\') ++len;
	return std::string(f.c_str(), len);
}

// do not include files and folders whose
// name starts with a .
bool file_filter(std::string const& f)
{
	if (f.empty()) return false;

	char const* first = f.c_str();
	char const* sep = strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	char const* altsep = strrchr(first, '\\');
	if (sep == nullptr || altsep > sep) sep = altsep;
#endif
	// if there is no parent path, just set 'sep'
	// to point to the filename.
	// if there is a parent path, skip the '/' character
	if (sep == nullptr) sep = first;
	else ++sep;

	// return false if the first character of the filename is a .
	if (sep[0] == '.') return false;

	std::cerr << f << "\n";
	return true;
}

[[noreturn]] void print_usage()
{
	std::cerr << R"(usage: make_torrent FILE [OPTIONS]

Generates a torrent file from the specified file
or directory and writes it to standard out


OPTIONS:
-w url        adds a web seed to the torrent with
              the specified url
-t url        adds the specified tracker to the
              torrent. For multiple trackers, specify more
              -t options. Specify a dash character "-" as a tracker to indicate
              the following trackers should be in a higher tier.
-c comment    sets the comment to the specified string
-C creator    sets the created-by field to the specified string
-s bytes      specifies a piece size for the torrent
              This has to be a power of 2, minimum 16kiB
-l            Don't follow symlinks, instead encode them as
              links in the torrent file
-o file       specifies the output filename of the torrent file
              If this is not specified, the torrent file is
              printed to the standard out, except on windows
              where the filename defaults to a.torrent
-r file       add root certificate to the torrent, to verify
              the HTTPS tracker
-S info-hash  add a similar torrent by info-hash. The similar
              torrent is expected to share some files with this one
-L collection add a collection name to this torrent. Other torrents
              in the same collection is expected to share files
              with this one.
-2            Only generate V2 metadata
-T            Include file timestamps in the .torrent file.
-z url        adds a web seed to the torrent with
              the specified url, like -w, but use zip file
-Z file      output zip file path
)";
	std::exit(1);
}

} // anonymous namespace

int main(int argc_, char const* argv_[]) try
{
	lt::span<char const*> args(argv_, argc_);
	std::string creator_str = "libtorrent";
	std::string comment_str;

	if (args.size() < 2) print_usage();

	std::vector<std::string> web_seeds;
	std::vector<std::string> zip_web_seeds;
	std::string zip_output_path;
	std::vector<std::string> trackers;
	std::vector<std::string> collections;
	std::vector<lt::sha1_hash> similar;
	int piece_size = 0;
	lt::create_flags_t flags = {};
	std::string root_cert;

	std::string outfile;
#ifdef TORRENT_WINDOWS
	// don't ever write binary data to the console on windows
	// it will just be interpreted as text and corrupted
	outfile = "a.torrent";
#endif

	std::string full_path = args[1];
	args = args.subspan(2);

	for (; !args.empty(); args = args.subspan(1)) {
		if (args[0][0] != '-') print_usage();

		char const flag = args[0][1];

		switch (flag)
		{
			case 'l':
				flags |= lt::create_torrent::symlinks;
				continue;
			case '1':
				flags |= lt::create_torrent::v1_only;
				continue;
			case '2':
				flags |= lt::create_torrent::v2_only;
				continue;
			case 'T':
				flags |= lt::create_torrent::modification_time;
				continue;
		}

		if (args.size() < 2) print_usage();

		switch (flag)
		{
			case 'w': web_seeds.push_back(args[1]); break;
			case 'z': zip_web_seeds.push_back(args[1]); break;
			case 'Z': 
				zip_output_path = (args[1]); break;
			case 't': trackers.push_back(args[1]); break;
			case 's': piece_size = atoi(args[1]); break;
			case 'o': outfile = args[1]; break;
			case 'C': creator_str = args[1]; break;
			case 'c': comment_str = args[1]; break;
			case 'r': root_cert = args[1]; break;
			case 'L': collections.push_back(args[1]); break;
			case 'S': {
				if (strlen(args[1]) != 40) {
					std::cerr << "invalid info-hash for -S. "
						"Expected 40 hex characters\n";
					print_usage();
				}
				std::stringstream hash(args[1]);
				lt::sha1_hash ih;
				hash >> ih;
				if (hash.fail()) {
					std::cerr << "invalid info-hash for -S\n";
					print_usage();
				}
				similar.push_back(ih);
				break;
			}
			default:
				print_usage();
		}
		args = args.subspan(1);
	}

	lt::file_storage fs;
#ifdef TORRENT_WINDOWS
	if (full_path[1] != ':')
#else
	if (full_path[0] != '/')
#endif
	{
		char cwd[2048];
#ifdef TORRENT_WINDOWS
#define getcwd_ _getcwd
#else
#define getcwd_ getcwd
#endif

		char const* ret = getcwd_(cwd, sizeof(cwd));
		if (ret == nullptr) {
			std::cerr << "failed to get current working directory: "
				<< strerror(errno) << "\n";
			return 1;
		}

#undef getcwd_
#ifdef TORRENT_WINDOWS
		full_path = cwd + ("\\" + full_path);
#else
		full_path = cwd + ("/" + full_path);
#endif
	}

	lt::add_files(fs, full_path, file_filter, flags);
	if (fs.num_files() == 0) {
		std::cerr << "no files specified.\n";
		return 1;
	}

	lt::create_torrent t(fs, piece_size, flags);
	int tier = 0;
	for (std::string const& tr : trackers) {
		if (tr == "-") ++tier;
		else t.add_tracker(tr, tier);
	}

	for (std::string const& ws : web_seeds)
		t.add_url_seed(ws);

	for (std::string const& c : collections)
		t.add_collection(c);

	for (lt::sha1_hash const& s : similar)
		t.add_similar_torrent(s);

	auto const num = t.num_pieces();
	lt::set_piece_hashes(t, branch_path(full_path)
		, [num] (lt::piece_index_t const p) {
			std::cerr << "\r" << p << "/" << num;
		});

	std::cerr << "\n";
	t.set_creator(creator_str.c_str());
	if (!comment_str.empty()) {
		t.set_comment(comment_str.c_str());
	}

	if (!root_cert.empty()) {
		std::vector<char> const pem = load_file(root_cert);
		t.set_root_cert(std::string(&pem[0], pem.size()));
	}

	auto entry = t.generate();

	if (!zip_web_seeds.empty()) {
		auto& zipinfo_e = entry["zipinfo"];
		if (zip_web_seeds.size() > 1) {
			std::vector<libtorrent::entry> seeds;
			for (auto&& url : zip_web_seeds)
			{
				seeds.push_back(url);
			}

			zipinfo_e["url-list"] = seeds;
		}
		else if (!zip_web_seeds.empty()) {
			zipinfo_e["url-list"] = zip_web_seeds[0];
		}
		
		std::string zipoutpath(full_path + ".dat");
		if (!zip_output_path.empty()) {
			zipoutpath = zip_output_path;
		}
		std::ofstream zipfile(zipoutpath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
		if (!zipfile.is_open()) {
			std::cerr << "zip output file open filed." << std::endl;
			return 1;
		}

		std::string pieces_buffer;
		std::string pieces_zip_buf;
		mz_ulong pieces_zip_size = 0;
		uint32_t net_size = 0;
		uint64_t total_zip_size = 0;
		int zip_level = MZ_HUFFMAN_ONLY;

		std::stringstream ss;
		for (int i = 0; i < t.files().num_pieces(); ++i)
		{
			pieces_zip_size = 0;
			pieces_buffer.clear();
			pieces_zip_buf.clear();
			auto index = libtorrent::piece_index_t(i);
			auto file_slices = t.files().map_block(index, 0, t.files().piece_size(index));
			for (auto&& slice : file_slices)
			{
				auto filePath = t.files().file_path(slice.file_index, full_path, false);
				std::ifstream slicefile(filePath.c_str(), std::ios::in | std::ios::binary);
				if (slicefile.is_open() == false)
				{
					std::cerr << "open file failed, " << filePath;
					return 1;
				}
				auto pre_buf_len = pieces_buffer.length();
				pieces_buffer.resize(pre_buf_len + slice.size);
				slicefile.seekg(slice.offset);
				slicefile.read((char*)(pieces_buffer.data() + pre_buf_len), slice.size);
			}
			//auto _sha1_ctx = lt::hasher();
			//_sha1_ctx.update(pieces_buffer.data(), pieces_buffer.size());
			//auto hash = lt::aux::to_hex(_sha1_ctx.final());
			pieces_zip_size = pieces_buffer.length() * 2;
			pieces_zip_buf.reserve(pieces_zip_size);
			auto ret = mz_compress2((unsigned char*)pieces_zip_buf.data(), &pieces_zip_size,
				(const unsigned char*)pieces_buffer.c_str(), pieces_buffer.length(), zip_level);
			if (ret != MZ_OK) {
				std::cerr << "zip piece failed, index: " << i << " err: " << ret;
				return 1;
			}
			net_size = libtorrent::aux::host_to_network(uint32_t(pieces_zip_size));
			zipfile.write(pieces_zip_buf.c_str(), pieces_zip_size);
			ss.write((char*)&net_size, sizeof(net_size));

			total_zip_size += pieces_zip_size;
		}
		zipfile.close();
		zipinfo_e["total size"] = total_zip_size;
		zipinfo_e["level"] = zip_level;
		zipinfo_e["pieces size"] = ss.str();
	}
	

	// create the torrent and print it to stdout
	std::vector<char> torrent;
	lt::bencode(back_inserter(torrent), entry);
	if (!outfile.empty()) {
		std::fstream out;
		out.exceptions(std::ifstream::failbit);
		out.open(outfile.c_str(), std::ios_base::out | std::ios_base::binary);
		out.write(torrent.data(), int(torrent.size()));
	}
	else {
		std::cout.write(torrent.data(), int(torrent.size()));
	}

	return 0;
}
catch (std::exception& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
	return 1;
}
