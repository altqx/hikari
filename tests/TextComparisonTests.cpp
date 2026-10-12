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
#include "../HikariSub/TextComparison.h"

#include <string>
#include <vector>

namespace {

struct Ranges {
	std::vector<int> first;
	std::vector<int> second;
};

// The Windows build: wxString holds UTF-16, a surrogate pair is two units.
Ranges Compare16(const std::u16string &first, const std::u16string &second)
{
	Ranges ranges;
	CHECK(CompareTextUnits(first.data(), first.size(), second.data(), second.size(), ranges.first, ranges.second)
		== TextComparisonResult::Compared);
	return ranges;
}

// The Linux build: wxString holds UTF-32, every character is one unit.
Ranges Compare32(const std::u32string &first, const std::u32string &second)
{
	Ranges ranges;
	CHECK(CompareTextUnits(first.data(), first.size(), second.data(), second.size(), ranges.first, ranges.second)
		== TextComparisonResult::Compared);
	return ranges;
}

// UTF-32 code point ranges as UTF-16 unit ranges of the same text.
std::vector<int> ToUtf16Units(const std::u32string &text, const std::vector<int> &ranges)
{
	std::vector<int> starts;
	int unit = 0;
	for (char32_t c : text) {
		starts.push_back(unit);
		unit += (c >= 0x10000) ? 2 : 1;
	}
	starts.push_back(unit);
	std::vector<int> units;
	for (size_t i = 0; i + 1 < ranges.size(); i += 2) {
		units.push_back(starts[ranges[i]]);
		units.push_back(starts[ranges[i + 1] + 1] - 1);
	}
	return units;
}

std::u16string ToUtf16(const std::u32string &text)
{
	std::u16string out;
	for (char32_t c : text) {
		if (c >= 0x10000) {
			out.push_back(static_cast<char16_t>(0xD800 + ((c - 0x10000) >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
		}
		else {
			out.push_back(static_cast<char16_t>(c));
		}
	}
	return out;
}

bool IsLowSurrogate(char16_t unit)
{
	return unit >= 0xDC00 && unit <= 0xDFFF;
}

bool IsHighSurrogate(char16_t unit)
{
	return unit >= 0xD800 && unit <= 0xDBFF;
}

// No UTF-16 range starts on a low surrogate or ends on a high one.
bool CoversWholeCharacters(const std::u16string &text, const std::vector<int> &ranges)
{
	for (size_t i = 0; i + 1 < ranges.size(); i += 2) {
		if (ranges[i] < 0 || ranges[i + 1] < ranges[i] || ranges[i + 1] >= static_cast<int>(text.size()))
			return false;
		if (IsLowSurrogate(text[ranges[i]]) || IsHighSurrogate(text[ranges[i + 1]]))
			return false;
	}
	return true;
}

} // namespace

TEST(text_comparison_marks_bmp_differences_by_character)
{
	Ranges ranges = Compare16(u"abcd", u"abxd");
	CHECK(ranges.first == std::vector<int>({ 2, 2 }));
	CHECK(ranges.second == std::vector<int>({ 2, 2 }));

	ranges = Compare32(U"abc", U"ac");
	CHECK(ranges.first == std::vector<int>({ 1, 1 }));
	CHECK(ranges.second.empty());

	ranges = Compare16(u"", u"ab");
	CHECK(ranges.first.empty());
	CHECK(ranges.second == std::vector<int>({ 0, 1 }));
}

TEST(text_comparison_marks_whole_surrogate_pair_in_utf16)
{
	// U+1F600 is D83D DE00 and U+1F601 is D83D DE01: they share the high
	// surrogate, which a unit-by-unit match would leave unmarked.
	Ranges ranges = Compare16(u"a\U0001F600b", u"a\U0001F601b");
	CHECK(ranges.first == std::vector<int>({ 1, 2 }));
	CHECK(ranges.second == std::vector<int>({ 1, 2 }));

	ranges = Compare32(U"a\U0001F600b", U"a\U0001F601b");
	CHECK(ranges.first == std::vector<int>({ 1, 1 }));
	CHECK(ranges.second == std::vector<int>({ 1, 1 }));
}

TEST(text_comparison_marks_inserted_non_bmp_character)
{
	Ranges ranges = Compare16(u"ab", u"a\U0001F600b");
	CHECK(ranges.first.empty());
	CHECK(ranges.second == std::vector<int>({ 1, 2 }));

	ranges = Compare16(u"x\U0001F600\U0001F600y", u"x\U0001F600y");
	CHECK(CoversWholeCharacters(u"x\U0001F600\U0001F600y", ranges.first));
	CHECK_EQ(ranges.first.size(), size_t(2));
	CHECK_EQ(ranges.first[1] - ranges.first[0], 1);
	CHECK(ranges.second.empty());
}

TEST(text_comparison_wchar_matches_the_build_encoding)
{
	// wxString stores wchar_t: UTF-16 on Windows, UTF-32 on Linux.
	std::wstring first = L"a\U0001F600b";
	std::wstring second = L"a\U0001F601b";
	std::vector<int> firstRanges, secondRanges;
	CHECK(CompareTextUnits(first.data(), first.size(), second.data(), second.size(), firstRanges, secondRanges)
		== TextComparisonResult::Compared);
	int last = sizeof(wchar_t) == 2 ? 2 : 1;
	CHECK(firstRanges == std::vector<int>({ 1, last }));
	CHECK(secondRanges == std::vector<int>({ 1, last }));
}

TEST(text_comparison_utf16_and_utf32_agree_by_character)
{
	const std::u32string pairs[][2] = {
		{ U"\U0001F600", U"\U0001F601" },
		{ U"Hello \U0001F600 world", U"Hello \U0001F601 word" },
		{ U"\U00020000\U00020001\U00020002", U"\U00020001" },
		{ U"\U0001F600\U0001F601\U0001F602abc", U"abc\U0001F602\U0001F600" },
		{ U"あ\U0001F600い", U"あ\U0001F600\U0001F600い" },
		{ U"{\\i1}\U0001D400x", U"{\\i1}\U0001D401x" },
	};
	for (const auto &pair : pairs) {
		std::u16string first16 = ToUtf16(pair[0]);
		std::u16string second16 = ToUtf16(pair[1]);
		Ranges utf16 = Compare16(first16, second16);
		Ranges utf32 = Compare32(pair[0], pair[1]);
		CHECK(utf16.first == ToUtf16Units(pair[0], utf32.first));
		CHECK(utf16.second == ToUtf16Units(pair[1], utf32.second));
		CHECK(CoversWholeCharacters(first16, utf16.first));
		CHECK(CoversWholeCharacters(second16, utf16.second));
	}
}

TEST(text_comparison_keeps_unpaired_surrogates_as_one_unit)
{
	std::u16string first = u"a";
	first.push_back(char16_t(0xD83D));
	first += u"b";
	Ranges ranges = Compare16(first, u"ab");
	CHECK(ranges.first == std::vector<int>({ 1, 1 }));
	CHECK(ranges.second.empty());

	std::u16string second = u"a";
	second.push_back(char16_t(0xDE00));
	ranges = Compare16(u"a\U0001F600", second);
	CHECK(ranges.first == std::vector<int>({ 1, 2 }));
	CHECK(ranges.second == std::vector<int>({ 1, 1 }));
}
