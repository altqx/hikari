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

#include "NaturalOrder.h"

#include <cwctype>

namespace {

bool IsDigit(wchar_t unit)
{
	return unit >= L'0' && unit <= L'9';
}

} // namespace

int NaturalCompare(std::wstring_view first, std::wstring_view second)
{
	size_t i = 0, j = 0;
	while (i < first.size() && j < second.size()) {
		if (IsDigit(first[i]) && IsDigit(second[j])) {
			// one number each: leading zeros skipped, then the longer run is
			// the larger number, and runs of one length compare digit by digit
			size_t endFirst = i, endSecond = j;
			while (endFirst < first.size() && IsDigit(first[endFirst]))
				++endFirst;
			while (endSecond < second.size() && IsDigit(second[endSecond]))
				++endSecond;
			size_t startFirst = i, startSecond = j;
			while (startFirst < endFirst && first[startFirst] == L'0')
				++startFirst;
			while (startSecond < endSecond && second[startSecond] == L'0')
				++startSecond;
			const size_t lengthFirst = endFirst - startFirst, lengthSecond = endSecond - startSecond;
			if (lengthFirst != lengthSecond)
				return lengthFirst < lengthSecond ? -1 : 1;
			for (size_t k = 0; k < lengthFirst; ++k) {
				if (first[startFirst + k] != second[startSecond + k])
					return first[startFirst + k] < second[startSecond + k] ? -1 : 1;
			}
			i = endFirst;
			j = endSecond;
			continue;
		}
		const std::wint_t a = std::towlower(static_cast<std::wint_t>(first[i]));
		const std::wint_t b = std::towlower(static_cast<std::wint_t>(second[j]));
		if (a != b)
			return a < b ? -1 : 1;
		++i;
		++j;
	}
	const bool firstLeft = i < first.size(), secondLeft = j < second.size();
	if (firstLeft != secondLeft)
		return firstLeft ? 1 : -1;
	// equal without case and as numbers: the plain name decides
	const int plain = first.compare(second);
	return plain < 0 ? -1 : plain > 0 ? 1 : 0;
}
