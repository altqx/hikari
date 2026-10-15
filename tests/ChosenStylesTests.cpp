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
#include "../HikariSub/ChosenStyles.h"

#include <string>

namespace {

// The Styles field of Find and replace or Shift times
struct StylesField {
	std::wstring value;
	int setCount = 0;
	void SetValue(const std::wstring &newValue)
	{
		value = newValue;
		++setCount;
	}
};

} // namespace

TEST(cancelling_the_style_chooser_keeps_the_styles_field)
{
	StylesField field{ L"Default,Signs" };
	// GetCheckedElements on Cancel
	CHECK(!PutChosenStyles(field, std::wstring()));
	CHECK(field.value == L"Default,Signs");
	CHECK_EQ(field.setCount, 0);
}

TEST(choosing_styles_puts_them_in_the_styles_field)
{
	StylesField field{ L"Default" };
	CHECK(PutChosenStyles(field, std::wstring(L"Signs,Top")));
	CHECK(field.value == L"Signs,Top");
	CHECK_EQ(field.setCount, 1);
}
