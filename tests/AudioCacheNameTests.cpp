//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

#include "check.h"
#include "../HikariSub/AudioCacheName.h"

#include <string>

namespace {

bool StartsWith(const std::wstring &text, const std::wstring &start)
{
	return text.compare(0, start.size(), start) == 0;
}

bool EndsWith(const std::wstring &text, const std::wstring &end)
{
	return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

}

TEST(two_videos_of_one_name_in_two_folders_get_two_audio_caches)
{
	CHECK(AudioCacheName(L"/home/a/Kage 07.mkv", 1, 2, 0) != AudioCacheName(L"/home/b/Kage 07.mkv", 1, 2, 0));
	CHECK(AudioCacheName(L"C:\\a\\Kage 07.mkv", 1, 2, 0) != AudioCacheName(L"C:\\b\\Kage 07.mkv", 1, 2, 0));
}

TEST(one_video_gets_the_same_audio_cache_each_time_it_is_opened)
{
	CHECK(AudioCacheName(L"/home/a/Kage 07.mkv", 1, 2, 0) == AudioCacheName(L"/home/a/Kage 07.mkv", 1, 2, 0));
}

TEST(audio_cache_name_shows_the_video_s_name_track_channels_and_delay)
{
	std::wstring name = AudioCacheName(L"/home/a/Kage 07.mkv", 1, 2, 0);
	CHECK(StartsWith(name, L"Kage 07_"));
	CHECK(EndsWith(name, L"_track1_2ch_0.w64"));
	CHECK(name.find(L'/') == std::wstring::npos);
	CHECK(StartsWith(AudioCacheName(L"C:\\a\\Kage 07.mkv", 1, 2, 0), L"Kage 07_"));
	CHECK(AudioCacheName(L"/home/a/Kage 07.mkv", 2, 2, 0) != name);
	CHECK(AudioCacheName(L"/home/a/Kage 07.mkv", 1, 6, 0) != name);
	CHECK(AudioCacheName(L"/home/a/Kage 07.mkv", 1, 2, -480) != name);
}

TEST(two_providers_make_one_audio_cache_under_two_names)
{
	std::wstring cache = AudioCacheName(L"/home/a/Kage 07.mkv", 1, 2, 0);
	std::wstring first = AudioCachePartName(cache, 100, 1);
	// a second tab in the same process, and a second instance of the program
	std::wstring secondTab = AudioCachePartName(cache, 100, 2);
	std::wstring secondProcess = AudioCachePartName(cache, 200, 1);
	CHECK(first != secondTab);
	CHECK(first != secondProcess);
	CHECK(secondTab != secondProcess);
	CHECK(StartsWith(first, cache));
	CHECK(EndsWith(first, L".part"));
	CHECK(EndsWith(secondTab, L".part"));
	CHECK(first != cache + L".part");
}
