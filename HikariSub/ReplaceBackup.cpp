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

#include "ReplaceBackup.h"

#include <cstdint>

namespace {

bool IsSeparator(wchar_t unit)
{
#ifdef _WIN32
	return unit == L'/' || unit == L'\\';
#else
	return unit == L'/';
#endif
}

// the folder name's part from the folder's own name, so that the user finds
// the backup; at most 32 units, which is 128 bytes of UTF-8 at most
const size_t nameLength = 32;

} // namespace

std::wstring ReplaceBackupFolderName(std::wstring_view subtitlesPath)
{
	std::wstring_view folder = subtitlesPath;
	size_t end = folder.size();
	while (end && !IsSeparator(folder[end - 1])) { end--; }
	while (end > 1 && IsSeparator(folder[end - 1])) { end--; }
	folder = folder.substr(0, end);

	size_t start = folder.size();
	while (start && !IsSeparator(folder[start - 1])) { start--; }
	std::wstring name(folder.substr(start, nameLength));
#ifdef _WIN32
	//not half a surrogate pair
	if (name.size() == nameLength && name.back() >= 0xD800 && name.back() <= 0xDBFF) { name.pop_back(); }
#endif
	for (wchar_t &unit : name) {
		if (unit < 32 || std::wstring_view(L"<>:\"/\\|?*").find(unit) != std::wstring_view::npos) { unit = L'_'; }
	}

	//FNV-1a of the whole path, folded to 32 bits
	uint64_t hash = 14695981039346656037ull;
	for (wchar_t unit : folder) {
		hash ^= static_cast<uint32_t>(unit);
		hash *= 1099511628211ull;
	}
	uint32_t folded = static_cast<uint32_t>(hash ^ (hash >> 32));
	const wchar_t *digits = L"0123456789abcdef";
	std::wstring hex(8, L'0');
	for (int i = 7; i >= 0; i--, folded >>= 4) { hex[i] = digits[folded & 15]; }

	return name.empty() ? hex : name + L' ' + hex;
}
