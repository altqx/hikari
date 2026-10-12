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
#include "../HikariSub/SrtTags.h"

#include <string>

namespace {

bool Removes(const std::wstring &text, const std::wstring &name, size_t from, size_t to,
	const std::wstring &expected, size_t expectedFrom, size_t expectedTo)
{
	SrtTagRemoval removal = RemoveSrtTagPairs(text, name, from, to);
	return removal.text == expected && removal.from == expectedFrom && removal.to == expectedTo
		&& removal.changed == (expected != text);
}

} // namespace

TEST(srt_tags_remove_pair_around_selection)
{
	// "foo <b>bar</b> baz" with "bar" selected.
	CHECK(Removes(L"foo <b>bar</b> baz", L"b", 7, 10, L"foo bar baz", 4, 7));
}

TEST(srt_tags_remove_pair_touching_caret_after_toolbar_insert)
{
	// PutinNonass leaves the caret just after </b> when it wraps a selection.
	CHECK(Removes(L"foo <b>bar</b> baz", L"b", 14, 14, L"foo bar baz", 7, 7));
	// And just after <b> when there is no selection and so no closing tag.
	CHECK(Removes(L"foo <b>bar baz", L"b", 7, 7, L"foo bar baz", 4, 4));
}

TEST(srt_tags_remove_pair_containing_caret)
{
	CHECK(Removes(L"<i>one two</i>", L"i", 6, 6, L"one two", 3, 3));
	CHECK(Removes(L"<I>one</I>", L"i", 4, 4, L"one", 1, 1));
}

TEST(srt_tags_keep_pairs_away_from_selection)
{
	CHECK(Removes(L"<b>a</b> mid <b>c</b>", L"b", 16, 16, L"<b>a</b> mid c", 13, 13));
	CHECK(Removes(L"<b>a</b> mid <b>c</b>", L"b", 10, 10, L"<b>a</b> mid <b>c</b>", 10, 10));
	CHECK(Removes(L"<b>a</b> mid <b>c</b>", L"b", 3, 4, L"a mid <b>c</b>", 0, 1));
	CHECK(Removes(L"plain <b>bold</b>", L"b", 2, 2, L"plain <b>bold</b>", 2, 2));
}

TEST(srt_tags_keep_other_tags)
{
	CHECK(Removes(L"<u><b>x</b><br></u>", L"b", 7, 7, L"<u>x<br></u>", 4, 4));
	CHECK(Removes(L"<font color=\"#fff\"><s>x</s></font>", L"s", 23, 23,
		L"<font color=\"#fff\">x</font>", 20, 20));
	CHECK(Removes(L"<u>x</u>", L"b", 0, 8, L"<u>x</u>", 0, 8));
}

TEST(srt_tags_clear_whole_line)
{
	std::wstring text = L"<b>a</b> b <b>c";
	CHECK(Removes(text, L"b", 0, text.size(), L"a b c", 0, 5));
}

TEST(srt_tags_remove_stray_closing_tag_at_selection)
{
	CHECK(Removes(L"ab</s>cd", L"s", 6, 6, L"abcd", 2, 2));
}

TEST(srt_tags_remove_nested_same_name_tags_as_one_span)
{
	// The inner pair belongs to the outer one, so no stray </b> is left.
	std::wstring text = L"<b>a<b>b</b>c</b>";
	CHECK(Removes(text, L"b", 7, 7, L"abc", 1, 1));
	CHECK(Removes(text, L"b", 0, text.size(), L"abc", 0, 3));
	CHECK(Removes(text, L"b", 7, 8, L"<b>a</b>b<b>c</b>", 8, 9));
	CHECK(Removes(L"<b>a</b></b>", L"b", 4, 4, L"a</b>", 1, 1));
}

TEST(srt_tags_keep_formatting_outside_a_partial_selection)
{
	// "bbb" selected: "aaa " stays bold.
	CHECK(Removes(L"<b>aaa bbb</b>", L"b", 7, 10, L"<b>aaa </b>bbb", 11, 14));
	// "bbb" selected in the middle: both sides stay bold.
	CHECK(Removes(L"<b>aaa bbb ccc</b>", L"b", 7, 10, L"<b>aaa </b>bbb<b> ccc</b>", 11, 14));
	// "aaa" selected at the start.
	CHECK(Removes(L"<b>aaa bbb</b>", L"b", 3, 6, L"aaa<b> bbb</b>", 0, 3));
	// An opening tag with no closing one opens again after the selection.
	CHECK(Removes(L"<b>aaa bbb", L"b", 3, 6, L"aaa<b> bbb", 0, 3));
	// Only other tags around the selection: nothing to keep bold.
	CHECK(Removes(L"<b><i>x</i></b>", L"b", 6, 7, L"<i>x</i>", 3, 4));
	// A selection that only touches the pair leaves it.
	CHECK(Removes(L"<b>a</b> mid", L"b", 8, 12, L"<b>a</b> mid", 8, 12));
}

TEST(srt_tags_caret_between_adjacent_pairs_removes_one)
{
	// The caret just after </b> is where PutinNonass leaves it.
	CHECK(Removes(L"<b>a</b><b>c</b>", L"b", 8, 8, L"a<b>c</b>", 1, 1));
}
