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

#include "TextComparison.h"

#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace {

// A text's code points and the code unit each one starts at; starts holds
// one more entry than points, the text's length in units.
struct CodePoints
{
	std::vector<char32_t> points;
	std::vector<size_t> starts;
};

template <typename Unit>
void Decode(const Unit *text, size_t length, CodePoints &out)
{
	static_assert(sizeof(Unit) == 2 || sizeof(Unit) == 4, "text must be UTF-16 or UTF-32");
	out.points.reserve(length);
	out.starts.reserve(length + 1);
	for (size_t i = 0; i < length;) {
		out.starts.push_back(i);
		char32_t unit = static_cast<char32_t>(static_cast<uint32_t>(text[i]));
		if constexpr (sizeof(Unit) == 2) {
			unit &= 0xFFFF;
			if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < length) {
				char32_t low = static_cast<char32_t>(static_cast<uint32_t>(text[i + 1]) & 0xFFFF);
				if (low >= 0xDC00 && low <= 0xDFFF) {
					out.points.push_back(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
					i += 2;
					continue;
				}
			}
		}
		out.points.push_back(unit);
		i++;
	}
	out.starts.push_back(length);
}

// The run of code points [firstPoint, lastPoint] as code unit indexes.
void PushRange(std::vector<int> &ranges, const CodePoints &text, size_t firstPoint, size_t lastPoint)
{
	ranges.push_back(static_cast<int>(text.starts[firstPoint]));
	ranges.push_back(static_cast<int>(text.starts[lastPoint + 1] - 1));
}

TextComparisonResult ComparePoints(const CodePoints &firstText, const CodePoints &secondText,
	std::vector<int> &firstRanges, std::vector<int> &secondRanges)
{
	const std::vector<char32_t> &first = firstText.points;
	const std::vector<char32_t> &second = secondText.points;
	size_t l1 = first.size(), l2 = second.size();
	if (l1 == std::numeric_limits<size_t>::max() || l2 == std::numeric_limits<size_t>::max() ||
		(l1 + 1) > std::numeric_limits<size_t>::max() / (l2 + 1)) {
		return TextComparisonResult::TooLarge;
	}
	size_t cellCount = (l1 + 1) * (l2 + 1);
	size_t w = l2 + 1;
	size_t i1, i2;
	std::vector<size_t> dpt;
	if (cellCount > dpt.max_size()) {
		return TextComparisonResult::OutOfMemory;
	}
	dpt.assign(cellCount, 0);

	// dpt[i1][i2]: the longest common subsequence of the last i1 code points
	// of first and the last i2 of second
	for (i1 = 1; i1 <= l1; i1++) {
		for (i2 = 1; i2 <= l2; i2++) {
			if (first[l1 - i1] == second[l2 - i2]) {
				dpt[w * i1 + i2] = dpt[w * (i1 - 1) + (i2 - 1)] + 1;
			}
			else if (dpt[w * (i1 - 1) + i2] > dpt[w * i1 + (i2 - 1)]) {
				dpt[w * i1 + i2] = dpt[w * (i1 - 1) + i2];
			}
			else {
				dpt[w * i1 + i2] = dpt[w * i1 + (i2 - 1)];
			}
		}
	}

	const size_t none = std::numeric_limits<size_t>::max();
	size_t sfirst = none, ssecond = none;
	i1 = l1; i2 = l2;
	for (;;) {
		if ((i1 > 0) && (i2 > 0) && (first[l1 - i1] == second[l2 - i2])) {
			if (sfirst != none) {
				PushRange(firstRanges, firstText, sfirst, (l1 - i1) - 1);
				sfirst = none;
			}
			if (ssecond != none) {
				PushRange(secondRanges, secondText, ssecond, (l2 - i2) - 1);
				ssecond = none;
			}
			i1--; i2--; continue;
		}
		else {
			if (i1 > 0 && (i2 == 0 || dpt[w * (i1 - 1) + i2] >= dpt[w * i1 + (i2 - 1)])) {
				if (sfirst == none) { sfirst = l1 - i1; }
				i1--; continue;
			}
			else if (i2 > 0 && (i1 == 0 || dpt[w * (i1 - 1) + i2] < dpt[w * i1 + (i2 - 1)])) {
				if (ssecond == none) { ssecond = l2 - i2; }
				i2--; continue;
			}
		}

		break;
	}
	if (sfirst != none) {
		PushRange(firstRanges, firstText, sfirst, (l1 - i1) - 1);
	}
	if (ssecond != none) {
		PushRange(secondRanges, secondText, ssecond, (l2 - i2) - 1);
	}
	return TextComparisonResult::Compared;
}

} // namespace

template <typename Unit>
TextComparisonResult CompareTextUnits(const Unit *first, size_t firstLength,
	const Unit *second, size_t secondLength,
	std::vector<int> &firstRanges, std::vector<int> &secondRanges)
{
	try {
		CodePoints firstText, secondText;
		Decode(first, firstLength, firstText);
		Decode(second, secondLength, secondText);
		return ComparePoints(firstText, secondText, firstRanges, secondRanges);
	}
	catch (const std::bad_alloc &) {
		return TextComparisonResult::OutOfMemory;
	}
	catch (const std::length_error &) {
		return TextComparisonResult::OutOfMemory;
	}
}

template TextComparisonResult CompareTextUnits<wchar_t>(const wchar_t *, size_t, const wchar_t *, size_t,
	std::vector<int> &, std::vector<int> &);
template TextComparisonResult CompareTextUnits<char16_t>(const char16_t *, size_t, const char16_t *, size_t,
	std::vector<int> &, std::vector<int> &);
template TextComparisonResult CompareTextUnits<char32_t>(const char32_t *, size_t, const char32_t *, size_t,
	std::vector<int> &, std::vector<int> &);
