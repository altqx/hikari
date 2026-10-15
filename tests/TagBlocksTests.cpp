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
#include "../HikariSub/TagBlocks.h"

#include <cstdio>
#include <string>

namespace {

// The tags blocks here are ASCII.
std::string Narrow(const std::wstring &text)
{
	return std::string(text.begin(), text.end());
}

bool Merges(const std::wstring &first, const std::wstring &wrap, const std::wstring &expected)
{
	std::wstring output = first;
	MergeTagBlocks(output, wrap);
	if (output == expected)
		return true;
	std::printf("  %s + %s gave %s\n", Narrow(first).c_str(), Narrow(wrap).c_str(), Narrow(output).c_str());
	return false;
}

} // namespace

TEST(tag_blocks_merge_adds_missing_tags_and_replaces_same_ones)
{
	CHECK(Merges(L"{\\an2\\b1}", L"{\\i1}", L"{\\an2\\b1\\i1}"));
	CHECK(Merges(L"{\\an2\\b0}", L"{\\b1}", L"{\\an2\\b1}"));
	CHECK(Merges(L"{\\pos(320,450)\\an2\\b1}", L"{\\i1\\b0}", L"{\\pos(320,450)\\an2\\b0\\i1}"));
	CHECK(Merges(L"{\\an2\\fnArial}", L"{\\fnTimes New Roman}", L"{\\an2\\fnTimes New Roman}"));
}

TEST(tag_blocks_merge_keeps_a_tag_whose_name_starts_another)
{
	// SubsGrid::Split by wraps of "{\bord2}ab\N{\b1}cd" lost the bold.
	CHECK(Merges(L"{\\an2\\bord2}", L"{\\b1}", L"{\\an2\\bord2\\b1}"));
	CHECK(Merges(L"{\\an2\\bord2\\b0}", L"{\\b1}", L"{\\an2\\bord2\\b1}"));
	CHECK(Merges(L"{\\an2\\shad3}", L"{\\s1}", L"{\\an2\\shad3\\s1}"));
	CHECK(Merges(L"{\\an2\\fscx120}", L"{\\fs40}", L"{\\an2\\fscx120\\fs40}"));
	CHECK(Merges(L"{\\an2\\fsp2}", L"{\\fs40\\fsp1}", L"{\\an2\\fsp1\\fs40}"));
}

TEST(tag_blocks_merge_treats_tag_aliases_as_one_tag)
{
	CHECK(Merges(L"{\\an2\\frz10}", L"{\\fr20}", L"{\\an2\\fr20}"));
	CHECK(Merges(L"{\\an2\\fr10}", L"{\\frz20}", L"{\\an2\\frz20}"));
	CHECK(Merges(L"{\\an2\\1c&H0000FF&}", L"{\\c&HFF0000&}", L"{\\an2\\c&HFF0000&}"));
	CHECK(Merges(L"{\\an2\\c&H0000FF&}", L"{\\1c&HFF0000&}", L"{\\an2\\1c&HFF0000&}"));
	CHECK(Merges(L"{\\an2\\1c&H0000FF&}", L"{\\1c&HFF0000&}", L"{\\an2\\1c&HFF0000&}"));
	// \3c is a colour of its own.
	CHECK(Merges(L"{\\an2\\3c&H0000FF&}", L"{\\c&HFF0000&}", L"{\\an2\\3c&H0000FF&\\c&HFF0000&}"));
}

TEST(tag_blocks_merge_into_an_empty_block_takes_the_merged_one)
{
	CHECK(Merges(L"", L"{\\b1}", L"{\\b1}"));
	CHECK(Merges(L"{}", L"{\\b1}", L"{\\b1}"));
	CHECK(Merges(L"{\\an2}", L"", L"{\\an2}"));
}
