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

#pragma once

#include <string>
#include <string_view>

// The file names of a video's audio cache in the AudioCache folder
// (ProviderFFMS2::AudioLoad and DiskCache).

// The finished cache: the video's name without its folder and extension,
// a hash of its whole path, so two videos of one name in two folders get two
// caches, then the track, the channels and the delay, as a cache made with
// other channels or another delay holds other data.
std::wstring AudioCacheName(std::wstring_view videoPath, int track, int channels, long long delayFrames);

// The name a provider writes the cache under until it is complete, then
// renames it to cacheName. It is the provider's own (its process and its
// number in that process), so a second tab on the same video makes its own
// cache and does not empty, or on Windows fail to open, the first tab's.
std::wstring AudioCachePartName(std::wstring_view cacheName, unsigned long processId, unsigned long number);
