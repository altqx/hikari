//  Copyright (c) 2016 - 2026, Marcin Drob

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

#include "MappedButton.h"
#include "Menu.h"

class MenuButton : public MappedButton
{
public:
	MenuButton(wxWindow *parent, int id, const wxString &tooltip, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
	~MenuButton(){if(menu){delete menu;}}
	//it own menu
	void PutMenu(Menu *menu);
	Menu *GetMenu(){return menu;}
private:
	void OnMouseEvent(wxMouseEvent &evt);
	bool IsMenuShown;
	Menu *menu;
};

