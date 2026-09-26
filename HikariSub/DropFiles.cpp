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


#include "DropFiles.h"
#include "HikariSubFrame.h"
#include "Notebook.h"
#include <wx/event.h>

DragnDrop::DragnDrop(HikariSubFrame* kfparent)
{
	Hikari = kfparent;
	int timerId = 8989;
	timer.SetOwner(Hikari, timerId);
	Hikari->Bind(wxEVT_TIMER, [=, this](wxTimerEvent &evt) { OnDropTimer(evt); }, timerId);
}

bool DragnDrop::OnDropFiles(wxCoord posx, wxCoord posy, const wxArrayString& filenames)
{
	files = filenames;
	x = posx;
	y = posy;
	timer.Start(50, true);
	return true;
}

void DragnDrop::OnDropTimer(wxTimerEvent & evt)
{
	if (files.size() > 1) {
		Hikari->OpenFiles(files);
	}
	else if (files.size() > 0) {
		wxString ext = files[0].AfterLast(L'.').Lower();
		bool isLuaScript = ext == L"lua" || ext == L"moon";
		int w, h;
		Hikari->Tabs->GetClientSize(&w, &h);
		if (!isLuaScript) {
			if (y >= h - Hikari->Tabs->GetHeight()) {
				int pixels;
				int tab = Hikari->Tabs->FindTab(x, &pixels);
				if (tab < 0) { Hikari->InsertTab(); }
				else if (Hikari->Tabs->iter != tab) { Hikari->Tabs->ChangePage(tab); }
			}
			else {
				int tabByPos = Hikari->Tabs->GetIterByPos(wxPoint(x, y));
				if (Hikari->Tabs->iter != tabByPos) { Hikari->Tabs->ChangePage(tabByPos); }
			}
		}
		Hikari->OpenFile(files[0]);
	}
}
