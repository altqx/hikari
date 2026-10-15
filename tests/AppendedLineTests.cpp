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
#include "../HikariSub/AppendedLine.h"

#include <cstddef>
#include <vector>

namespace {

size_t SourceOf(const std::vector<bool> &shown)
{
	return AppendedLineSource(shown.size(), [&shown](size_t i){ return shown[i]; });
}

} // namespace

TEST(the_appended_line_copies_the_last_line_when_none_is_hidden)
{
	CHECK_EQ(SourceOf({ true }), (size_t)0);
	CHECK_EQ(SourceOf({ true, true, true, true }), (size_t)3);
}

TEST(the_appended_line_copies_the_last_line_when_hidden_lines_come_before_it)
{
	// Line 1 shown, Lines 2 and 3 hidden, Line 4 shown and active: the copy
	// is of Line 4, not of the Line at the number of shown Lines before it
	CHECK_EQ(SourceOf({ true, false, false, true }), (size_t)3);
	CHECK_EQ(SourceOf({ false, false, true }), (size_t)2);
}

TEST(the_appended_line_copies_the_last_shown_line_when_the_last_lines_are_hidden)
{
	CHECK_EQ(SourceOf({ true, true, false }), (size_t)1);
	CHECK_EQ(SourceOf({ false, true, false, false }), (size_t)1);
	CHECK_EQ(SourceOf({ true, false, true, false, false }), (size_t)2);
}

TEST(the_appended_line_copies_the_last_line_when_no_line_is_shown)
{
	CHECK_EQ(SourceOf({ false }), (size_t)0);
	CHECK_EQ(SourceOf({ false, false, false }), (size_t)2);
}
