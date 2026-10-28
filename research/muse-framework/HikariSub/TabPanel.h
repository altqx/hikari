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

#include "HikariPanel.h"
//#include "ShiftTimes.h"
#include "VideoBox.h"
#include "HikariSubFrame.h"
#include "TabPanel.h"
#include "HikariWindowResizer.h"
#include "SubsGrid.h"
#include "EditBox.h"
#include "WinUndef.h"
#include <wx/sizer.h>

class ShiftTimes;

class TabPanel : public HikariPanel
{
public:
	TabPanel(wxWindow *parent, HikariSubFrame *hikari, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);
	virtual ~TabPanel();
	bool Hide();
	
	SubsGrid* grid;
	EditBox* edit;
	VideoBox* video;
	ShiftTimes* shiftTimes;

	wxBoxSizer* MainSizer;
	wxBoxSizer* VideoEditboxSizer;
	wxBoxSizer* GridShiftTimesSizer;

	void SetAccels(bool onlyGridAudio = false);
	void SetVideoWindowSizes(int w, int h, bool allTabs);
	bool SetFont(const wxFont &font);
	void SetLastSaveTime();
	void ReloadSubsIfModified();
	// catches up with what the options dialog changed
	void ApplyOptionChanges(bool gridFont, bool gridColours, bool audio);

	bool editor;
	bool audioHotkeysLoaded = false;


	wxString SubsName;
	wxString VideoName;
	wxString SubsPath;
	wxString VideoPath;
	wxString AudioPath;
	wxString KeyframesPath;
	int lastFocusedWindowId = 0;
	HikariWindowResizer* windowResizer;
private:

	bool holding;
	void OnFocus(wxChildFocusEvent& event);
	void OnSize(wxSizeEvent & evt);
	SYSTEMTIME lastSave;
	bool blockRemovedFile = false;
	DECLARE_EVENT_TABLE()
};

