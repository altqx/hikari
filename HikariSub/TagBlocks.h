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

// The name of an override tag written without its backslash: "bord" for
// "bord2", "1c" for "1c&H0000FF&", "fn" for "fnArial". The name ends at the
// first digit (after the first character), '-', '.', '(' or '&'.
inline std::wstring OverrideTagName(const std::wstring &tagWithValue)
{
	if (tagWithValue.compare(0, 2, L"fn") == 0)
		return L"fn";
	if (tagWithValue.empty())
		return std::wstring();

	size_t end = tagWithValue.find_first_of(L"0123456789-.(&", 1);
	return tagWithValue.substr(0, end);
}

// \c and \1c are one tag, as are \fr and \frz.
inline std::wstring SameOverrideTag(const std::wstring &name)
{
	if (name == L"1c")
		return L"c";
	if (name == L"frz")
		return L"fr";
	return name;
}

// Merges the tags of blockToMerge ("{\b1\i1}") into output, the tags block
// of a line (SubsGrid::Split puts a later wrap's tags into the first one):
// each tag replaces the first tag of output with the same name (\c and \1c,
// \fr and \frz alike) and is added before output's last '}' when there is
// none. A tag only replaces one of its own name, so \b1 goes in beside
// \bord2 and \s1 beside \shad3. An output shorter than "{}" plus a tag
// becomes blockToMerge.
inline void MergeTagBlocks(std::wstring &output, const std::wstring &blockToMerge)
{
	if (blockToMerge.empty())
		return;

	if (output.size() < 3) {
		output = blockToMerge;
		return;
	}

	const std::wstring tags = blockToMerge.size() < 2 ? std::wstring() : blockToMerge.substr(1, blockToMerge.size() - 2);
	for (size_t pos = 0; pos < tags.size();) {
		size_t next = tags.find(L'\\', pos);
		if (next == std::wstring::npos)
			next = tags.size();
		if (next == pos) {
			++pos;
			continue;
		}
		const std::wstring tag = tags.substr(pos, next - pos);
		pos = next + 1;
		const std::wstring name = SameOverrideTag(OverrideTagName(tag));

		// Every tag of output runs from its backslash to the next backslash or '}'.
		bool replaced = false;
		for (size_t start = output.find(L'\\'); start != std::wstring::npos; start = output.find(L'\\', start + 1)) {
			size_t end = output.find_first_of(L"\\}", start + 1);
			if (end == std::wstring::npos)
				end = output.size();
			if (SameOverrideTag(OverrideTagName(output.substr(start + 1, end - start - 1))) == name) {
				output.replace(start, end - start, L"\\" + tag);
				replaced = true;
				break;
			}
		}
		if (!replaced)
			output.insert(output.size() - 1, L"\\" + tag);
	}
}
