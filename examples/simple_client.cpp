/*

Copyright (c) 2003, 2005, 2009, 2015-2017, 2019-2020, Arvid Norberg
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

#include <cstdlib>
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
using namespace std::chrono;
using namespace std::chrono_literals;


struct file_pointer
{
	file_pointer() : ptr(nullptr) {
		OutputDebugStringA(("file open null: " + std::to_string(uint64_t(ptr)) + " \r\n").c_str());
	}
	explicit file_pointer(FILE* p) : ptr(p) {
		OutputDebugStringA(("file open: " + std::to_string(uint64_t(ptr)) + " \r\n").c_str());
	}
	~file_pointer() {
		bool closeSuc = false;
		if (ptr != nullptr) {
			closeSuc = ::fclose(ptr);
		}
		OutputDebugStringA(("file close: " + std::to_string(uint64_t(ptr))
			+ " suc: " + std::to_string(closeSuc)
			+ " e: " + std::to_string(GetLastError()) + " \r\n").c_str());
	}
	file_pointer(file_pointer const&) = delete;
	file_pointer(file_pointer&& f) : ptr(f.ptr) { f.ptr = nullptr; }
	file_pointer& operator=(file_pointer const&) = delete;
	file_pointer& operator=(file_pointer&& f)
	{
		std::swap(ptr, f.ptr);
		return *this;
	}
	FILE* file() const { return ptr; }
private:
	FILE* ptr;
};

file_pointer open_file()
{

	auto path = LR"(\\?\D:\Users\s3808\source\repos\p2p\libtorrent\build_win32_static\examples\tutorials_demo-bin_v4.2.19.2\npl-demo.exe)";
	FILE* f = ::_wfopen(path, L"ab+");
	std::wstringstream ss;
	ss << "file " << " write " << ": " << std::to_wstring(uint64_t(f))
		<< " errno: " << GetLastError() << std::endl;
	OutputDebugStringW(ss.str().c_str());
	if (f == nullptr) {
		for (int i = 0; i < 30; ++i)
		{
			f = ::_wfopen(path, L"ab+");
			if (f)
				break;
			std::this_thread::sleep_for(30ms);
		}
	}
	if (f == nullptr)
	{
		if (GetLastError() == 2) {
			f = ::_wfopen(path, L"wb+");

			if (f == nullptr)
			{
				return file_pointer{};
			}
		}
		else
		{
			return file_pointer{};
		}
	}

	return file_pointer{ f };
}

bool fopenerr(const wchar_t* path) {
	auto index = 0, max_errtimes = 20;
	while (++index && max_errtimes > 0)
	{
		FILE* f = nullptr;
		f = ::_wfopen(path, L"ab+");
		//if (f == nullptr) {
		//	for (int i = 0; i < 30; ++i)
		//	{
		//		f = ::_wfopen(path, L"ab+");
		//		std::cout << "reopen: " << index << " times: " << i << std::endl;
		//		if (f)
		//			break;
		//		std::this_thread::sleep_for(30ms);
		//	}
		//}

		if (f == nullptr) {
			std::cerr << index << ": open file failed, errno: " << errno << " " << strerror(errno) << std::endl;
			max_errtimes--;
			continue;
		}
		std::string s("hellooooooooooooooooooooooooooooooooooooooo");
		::fwrite(s.c_str(), 1, s.length(), f);
		fclose(f);
	}
	return max_errtimes == 20;
}

#include <filesystem>
namespace fs = std::experimental::filesystem;

int main(int argc, char* argv[]) try
{
	
	//wchar_t fullp[_MAX_PATH]{};
	//std::wstring path = LR"(.\npl-demo.exe)";
	//_wfullpath(fullp, path.c_str(), _MAX_PATH);
	//path = fullp;

	//fs::path pf(path);
	//if (!fs::exists(pf.parent_path())) {
	//	fs::create_directories(pf.parent_path());
	//}

	//auto path1 = path;
	//std::wcout << L"check path: " << path1.c_str() << std::endl;
	//if (!fopenerr(path1.c_str()))
	//	return 1;


	if (argc != 2) {
		std::cerr << "usage: ./simple_client torrent-file\n"
			"to stop the client, press return.\n";
		return 1;
	}
	std::ofstream of("simple_client.log", std::ios::trunc | std::ios::out);
	if (!of.is_open()) {
		std::cout << "log open failed" << std::endl;
	}

	lt::settings_pack setting;
	setting.set_int(lt::settings_pack::alert_mask
		, lt::alert_category::error
		| lt::alert_category::storage
		| lt::alert_category::status
		| lt::alert_category::stats
		| lt::alert_category::torrent_log);

	lt::session s(setting);
	lt::add_torrent_params p;
	p.save_path = ".";
	p.ti = std::make_shared<lt::torrent_info>(argv[1]);
	s.add_torrent(p);
	// wait for the user to end

	bool finished = false;
	while (!finished)
	{
		std::vector<lt::alert*> alerts;
		s.pop_alerts(&alerts); // 查询全部通知
		s.post_torrent_updates(); // state_update_alert 需要先点用 post_torrent_updates


		for (lt::alert const* a : alerts) {
			of << "recv alert: " << a->category() << " what: " << a->what() << " recv: " << a->message() << std::endl;
			std::cout << "recv alert: " << a->category() << " what: " << a->what() << " recv: " << a->message() << std::endl;
			if (auto st = lt::alert_cast<lt::torrent_finished_alert>(a)) {
				of << a->what() << " : " << a->category() << " : " << a->message() << std::endl;
				finished = true;
			}
			if (auto st = lt::alert_cast<lt::torrent_error_alert>(a)) {
				of << a->what() << " : " << a->category() << " : " << a->message() << std::endl;
			}

			if (auto st = lt::alert_cast<lt::state_update_alert>(a)) {
				if (st->status.empty()) continue;

				// we only have a single torrent, so we know which one
				// the status is for
				lt::torrent_status const& stus = st->status[0];
				of << int(stus.state) << ' '
					<< (stus.download_payload_rate / 1000) << " kB/s "
					<< (stus.total_done / 1000) << " kB ("
					<< (stus.progress_ppm / 10000) << "%) downloaded ("
					<< stus.num_peers << " peers)" << std::endl;
			}
		}
		of.flush();
		std::this_thread::sleep_for(1s);
	}
	of.close();
	//char a;
	//int ret = std::scanf("%c\n", &a);
	//(void)ret; // ignore
	return 0;
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
}
