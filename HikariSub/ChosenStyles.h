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

// Puts the styles chosen in the Choose styles window (GetCheckedElements)
// into a dialog's Styles field. GetCheckedElements returns nothing both on
// Cancel and when no style is checked: then the field keeps the styles it
// has. Returns whether the field got the chosen styles.
template <typename Field, typename String>
bool PutChosenStyles(Field &stylesField, const String &chosen)
{
	if (chosen.empty())
		return false;
	stylesField.SetValue(chosen);
	return true;
}
