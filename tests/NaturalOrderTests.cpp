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
#include "../HikariSub/NaturalOrder.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

int Sign(int value)
{
	return value < 0 ? -1 : value > 0 ? 1 : 0;
}

std::vector<std::wstring> Sorted(std::vector<std::wstring> names)
{
	std::sort(names.begin(), names.end(),
		[](const std::wstring &a, const std::wstring &b) { return NaturalCompare(a, b) < 0; });
	return names;
}

bool SameList(const std::vector<std::wstring> &actual, const std::vector<std::wstring> &expected)
{
	if (actual == expected)
		return true;
	std::printf("got:");
	for (const std::wstring &name : actual)
		std::printf(" [%ls]", name.c_str());
	std::printf("\n");
	return false;
}

} // namespace

// The previous / next file order: digit runs as numbers, so episode 2 comes
// before episode 10 whatever order the folder lists them in.
TEST(NaturalOrderEpisodesByNumber)
{
	CHECK(NaturalCompare(L"Ep 2.mkv", L"Ep 10.mkv") < 0);
	CHECK(NaturalCompare(L"Ep 10.mkv", L"Ep 2.mkv") > 0);
	CHECK(NaturalCompare(L"ep9.mkv", L"ep10.mkv") < 0);
	CHECK(SameList(Sorted({ L"/v/Show - 10.mkv", L"/v/Show - 1.mkv", L"/v/Show - 100.mkv", L"/v/Show - 2.mkv",
		L"/v/Show - 9.mkv", L"/v/Show - 11.mkv" }),
		{ L"/v/Show - 1.mkv", L"/v/Show - 2.mkv", L"/v/Show - 9.mkv", L"/v/Show - 10.mkv", L"/v/Show - 11.mkv",
			L"/v/Show - 100.mkv" }));
	// several numbers in a name: each run compared on its own
	CHECK(SameList(Sorted({ L"S2E1.mkv", L"S10E1.mkv", L"S1E10.mkv", L"S1E2.mkv" }),
		{ L"S1E2.mkv", L"S1E10.mkv", L"S2E1.mkv", L"S10E1.mkv" }));
	// numbers longer than any integer type still compare by value
	CHECK(NaturalCompare(L"x99999999999999999999999.mkv", L"x100000000000000000000000.mkv") < 0);
	// a digit against a letter compares as characters ('0'-'9' before letters)
	CHECK(NaturalCompare(L"a1.mkv", L"ab.mkv") < 0);
	CHECK(NaturalCompare(L"a.mkv", L"a1.mkv") < 0);
}

// Letters without case: "b" between "A" and "C".
TEST(NaturalOrderIgnoresCase)
{
	CHECK(SameList(Sorted({ L"c.MKV", L"B.mkv", L"a.mkv", L"D.mkv" }), { L"a.mkv", L"B.mkv", L"c.MKV", L"D.mkv" }));
	CHECK(NaturalCompare(L"ep 3.mkv", L"Ep 4.mkv") < 0);
	CHECK(NaturalCompare(L"EP 5.mkv", L"ep 4.mkv") > 0);
}

// Equal without case or as numbers: the plain name decides, so the order is
// total and one name is never equal to another.
TEST(NaturalOrderTiesByPlainName)
{
	CHECK(NaturalCompare(L"A.mkv", L"a.mkv") < 0); // 'A' (0x41) before 'a' (0x61)
	CHECK(NaturalCompare(L"a.mkv", L"A.mkv") > 0);
	CHECK(NaturalCompare(L"Ep 01.mkv", L"Ep 1.mkv") < 0); // '0' before '1'
	CHECK(NaturalCompare(L"Ep 1.mkv", L"Ep 01.mkv") > 0);
	CHECK(NaturalCompare(L"Ep 01.mkv", L"Ep 2.mkv") < 0); // the number first
	CHECK_EQ(NaturalCompare(L"Ep 1.mkv", L"Ep 1.mkv"), 0);
	CHECK_EQ(NaturalCompare(L"", L""), 0);
	CHECK(NaturalCompare(L"", L"a") < 0);
	CHECK(SameList(Sorted({ L"ep 1.mkv", L"Ep 1.mkv", L"Ep 01.mkv", L"EP 001.mkv" }),
		{ L"EP 001.mkv", L"Ep 01.mkv", L"Ep 1.mkv", L"ep 1.mkv" }));
}

// A strict weak order over a mixed folder: every pair answers both ways
// consistently and the sort is the same from any starting order. The
// non-ASCII names are written as escapes: MSVC reads a source file without a
// byte order mark in the ANSI code page.
TEST(NaturalOrderIsConsistent)
{
	const std::vector<std::wstring> names{ L"Ep 10.mkv", L"ep 2.mkv", L"Ep 02.mkv", L"Ep 2.MKV", L"ep 1.mkv",
		L"notes.txt", L"Ep.mkv", L"Ep 2a.mkv", L"Ep 2 v2.mkv", L"_extra.mkv", L"~tilde.avi", L"0.mkv", L"00.mkv",
		L"\u00c4rger.mkv", L"\u00e9.mkv", L"\u65e5\u672c\u8a9e.mkv" };
	for (const std::wstring &a : names) {
		CHECK_EQ(NaturalCompare(a, a), 0);
		for (const std::wstring &b : names) {
			if (a != b) {
				CHECK(NaturalCompare(a, b) != 0);
				CHECK_EQ(Sign(NaturalCompare(a, b)), -Sign(NaturalCompare(b, a)));
			}
			for (const std::wstring &c : names) {
				if (NaturalCompare(a, b) < 0 && NaturalCompare(b, c) < 0)
					CHECK(NaturalCompare(a, c) < 0);
			}
		}
	}
	std::vector<std::wstring> reversed(names.rbegin(), names.rend());
	CHECK(SameList(Sorted(reversed), Sorted(names)));
	CHECK(SameList(Sorted(names),
		{ L"0.mkv", L"00.mkv", L"_extra.mkv", L"ep 1.mkv", L"Ep 2 v2.mkv", L"Ep 02.mkv", L"Ep 2.MKV", L"ep 2.mkv",
			L"Ep 2a.mkv", L"Ep 10.mkv", L"Ep.mkv", L"notes.txt", L"~tilde.avi", L"\u00c4rger.mkv", L"\u00e9.mkv",
			L"\u65e5\u672c\u8a9e.mkv" }));
}
