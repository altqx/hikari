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
#include "TabPanel.h"
#include "MappedButton.h"
#include "NumCtrl.h"
#include "ListControls.h"
#include "Notebook.h"
#include "HikariSubFrame.h"
#include "HikariRadioButton.h"
#include "HikariCheckBox.h"
#include "TimeCtrl.h"
#include "HikariScrollbar.h"
#include "HikariStaticBoxSizer.h"
#include "WinUndef.h"
//#undef GetProfileString

class ShiftTimes: public HikariPanel
{
public:
	
	ShiftTimes(wxWindow* parent, HikariSubFrame* kfparent, wxWindowID id = -1, 
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style=0);
	virtual ~ShiftTimes();
	HikariRadioButton* StartVAtime;
	HikariRadioButton* EndVAtime;
	HikariChoice *WhichLines;
	HikariChoice *WhichTimes;
	HikariChoice *ProfilesList;
	HikariRadioButton* Forward;
	HikariRadioButton* Backward;
	HikariCheckBox* DisplayFrames;
	HikariCheckBox* MoveTagTimes;

	MappedButton* AddStyles;
	MappedButton* MoveTime;
	MappedButton* NewProfile;
	MappedButton* RemoveProfile;
	TimeCtrl* TimeText;
	HikariTextCtrl* Stylestext;
	HikariCheckBox* MoveToVideoTime;
	HikariCheckBox* MoveToAudioTime;
	HikariChoice* EndTimeCorrection;
	//postprocessor controls
	HikariCheckBox* LeadIn;
	HikariCheckBox* LeadOut;
	HikariCheckBox* Continous;
	HikariCheckBox* SnapKF;
	NumCtrl* LITime;
	NumCtrl* LOTime;
	NumCtrl* ThresStart;
	NumCtrl* ThresEnd;
	NumCtrl* BeforeStart;
	NumCtrl* AfterStart;
	NumCtrl* BeforeEnd;
	NumCtrl* AfterEnd;

	HikariStaticBoxSizer *liosizer;
	HikariStaticBoxSizer *consizer;
	HikariStaticBoxSizer *snapsizer;
	HikariStaticBoxSizer *profileSizer;
	HikariScrollbar *scroll;
	wxWindow *panel; 

	void Contents(bool addopts = true);
	void RefVals(ShiftTimes *from = nullptr);
	void OnOKClick(wxCommandEvent& event);
	wxBoxSizer *Main;
	bool SetBackgroundColour(const wxColour &col);
	bool SetForegroundColour(const wxColour &col);
	bool SetFont(const wxFont &font);

private:

	char form;
	HikariSubFrame* Hikari;
	bool isscrollbar;
	bool resizing;
	int scPos;
	MappedButton *coll;
	TabPanel *tab = nullptr;

	void OnAddStyles(wxCommandEvent& event);
	void OnChangeDisplayUnits(wxCommandEvent& event);
	void OnAddProfile(wxCommandEvent& event);
	void OnRemoveProfile(wxCommandEvent& event);
	void OnChangeProfile(wxCommandEvent& event);
	void OnEdition(wxCommandEvent& event);
	void ChangeDisplayUnits(bool times);
	void OnSize(wxSizeEvent& event);
	void OnScroll(wxScrollEvent& event);
	void OnMouseScroll(wxMouseEvent& event);
	void AudioVideoTime(wxCommandEvent &event);
	void CollapsePane(wxCommandEvent &event);
	void DoTooltips(bool normal = true);
	void SaveOptions();
	void CreateControls(bool normal = true);
	void GetProfilesNames(wxArrayString &list);
	void CreateProfile(const wxString &name, bool overwrite = false);
	void GetProfileString(const wxString& name, wxString* profileString);
	void ChangeProfileIfIsSet();
	void SetProfile(const wxString &name);

	DECLARE_EVENT_TABLE()
};


enum{
	ID_RADIOBUTTON1 = 11124, 
	ID_RADIOBUTTON2,
	ID_BSTYLE,
	ID_CLOSE,
	ID_SCROLL,
	ID_VIDEO,
	ID_AUDIO
};

