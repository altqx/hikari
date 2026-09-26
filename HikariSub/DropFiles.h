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



#include <wx/dnd.h>
#include <wx/timer.h>
#include <wx/arrstr.h>
class HikariSubFrame;

class DragnDrop : public wxFileDropTarget
{
	private:
	HikariSubFrame* Kai;
	wxTimer timer;
	wxArrayString files;
	int x, y;
	public:
	DragnDrop(HikariSubFrame* kfparent);
	virtual ~DragnDrop(){ };
	bool OnDropFiles(wxCoord posx, wxCoord posy, const wxArrayString& filenames);
	void OnDropTimer(wxTimerEvent &evt);
};




