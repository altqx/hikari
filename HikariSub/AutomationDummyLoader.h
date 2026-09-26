//  Copyright (c) 2026, Marcin Drob

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
// 
// #pragma once

#include <vector>
#include <wx/string.h>


namespace Auto {

	class LuaCommand;
	class LuaScript;

	class AutomationDummyLoader {

	public:
		std::vector<LuaScript*> Scripts;
		wxString AutoloadPath;
		
		AutomationDummyLoader() {};
		void FastReloadScripts();
		void SaveDummy();
		bool LoadDummy();
	private:
		void AddDummy(const wxString& filename, const wxString & name, const wxString& description, const std::vector<wxString> &macros,
			unsigned long lowTime, unsigned long highTime);
		void GetNameValue(const wxString& input);
		void ParseDummy(const wxString& input);
		size_t FindFilename(const wxString& filename);
		wxString filename;
		wxString name;
		wxString description;
		wxString lastLabel;
		unsigned long lowTime = 0;
		unsigned long highTime = 0;
		std::vector<wxString> macros;
		bool parseMacro = false;
	};


}
