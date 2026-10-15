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

#include "AudioCacheName.h"

#include <cstdint>

namespace
{
	std::wstring_view NameWithoutFolderAndExtension(std::wstring_view path)
	{
		size_t slash = path.find_last_of(L"/\\");
		if (slash != std::wstring_view::npos)
			path.remove_prefix(slash + 1);
		size_t dot = path.rfind(L'.');
		if (dot != std::wstring_view::npos && dot > 0)
			path = path.substr(0, dot);
		return path;
	}

	// FNV-1a of the path's code units: the same path always gives the same
	// hash, so a video opened again finds its cache
	std::uint32_t PathHash(std::wstring_view path)
	{
		std::uint32_t hash = 2166136261u;
		for (wchar_t unit : path) {
			std::uint32_t value = static_cast<std::uint32_t>(unit);
			for (int byte = 0; byte < 4; byte++) {
				hash ^= (value >> (byte * 8)) & 0xFF;
				hash *= 16777619u;
			}
		}
		return hash;
	}

	std::wstring Hex(std::uint32_t value)
	{
		std::wstring text(8, L'0');
		for (int digit = 7; digit >= 0; digit--, value >>= 4)
			text[digit] = L"0123456789abcdef"[value & 0xF];
		return text;
	}
}

std::wstring AudioCacheName(std::wstring_view videoPath, int track, int channels, long long delayFrames)
{
	std::wstring name(NameWithoutFolderAndExtension(videoPath));
	name += L"_" + Hex(PathHash(videoPath)) + L"_track" + std::to_wstring(track) + L"_" +
		std::to_wstring(channels) + L"ch_" + std::to_wstring(delayFrames) + L".w64";
	return name;
}

std::wstring AudioCachePartName(std::wstring_view cacheName, unsigned long processId, unsigned long number)
{
	return std::wstring(cacheName) + L"." + std::to_wstring(processId) + L"-" + std::to_wstring(number) + L".part";
}
