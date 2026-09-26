//  Copyright (c) 2020 - 2026, Marcin Drob

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

#include <wx/string.h>
#include <map>
#include <utility>

class ConfigConverter{
public:
	void CreateTable();
	bool ConvertConfig(wxString *rawConfig);
	bool ConvertColors(wxString *rawColors);
	bool ConvertHotkeys(wxString *rawHotkeys);
	static ConfigConverter *Get();
private:
	ConfigConverter(){};
	~ConfigConverter(){};
	ConfigConverter(const ConfigConverter &copy) = delete;
	std::map<const wxString, wxString> convertColors;
	std::map<const wxString, std::pair<wxString, wxString>> convertConfig;
	std::map<const wxString, wxString> convertHotkeys;
	static ConfigConverter ccthis;
};
