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
#include "WinUndef.h"
#include <wx/window.h>
#include "SubsTime.h"
#include "HikariTextCtrl.h"
#include "TimeCtrlSteps.h"
//#include "VideoBox.h"

class VideoBox;

class TimeCtrl : public HikariTextCtrl
{
public:
	TimeCtrl(wxWindow* parent, const long int id, const wxString& val = L"0:00:00.00", 
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, 
		long style = 0, const wxValidator& validator = wxDefaultValidator, const wxString& name = emptyString);
	virtual ~TimeCtrl();
	void SetVideoBox(VideoBox *_vb){ vb = _vb; };
	//0 dont setup frame, 1 start frame, 2 end frame,
	void SetTime(const SubsTime &newtime, bool stillModified = false, int opt = 0);
	//0 nothing, 1 -halframe (start), 2 +halfframe (end),
	SubsTime GetTime(char opt = 0);
	void ChangeFormat(char frm, float fps = 0);
	char GetFormat();
	bool HasShownFrames(){ return showFrames; }
	void ShowFrames(bool show = true){ showFrames = show; }

	//void SetModified(bool modified);
	bool changedBackGround;
private:
	void OnMouseLost(wxMouseCaptureLostEvent& event){ if (HasCapture()){ ReleaseMouse(); }; holding = false; };
	char form;
	SubsTime mTime;
	bool pastes;
	bool holding;
	int oldposy;
	int oldposx;
	int curpos;
	//the frame number or time the drag steps, and how
	int dragValue;
	TimeCtrlSteps::Step dragStep;
	bool showFrames;
	bool timeUnchanged;
	//the Frames box is on and frames can be shown: without a video, or with
	//one whose frame times are known; else the field shows times
	bool ShowsFrames();
	//FRAME when the field shows frames, else the subtitle format
	char ShownFormat();
	void OnTimeWrite(wxCommandEvent& event);
	void OnMouseEvent(wxMouseEvent &event);
	TimeCtrlSteps::Step StepAt(long column);
	void SetStepped(bool frames, int value);
	void OnKeyEvent(wxKeyEvent& event);
	//void OnPaste(wxCommandEvent &event);
	//void OnCopy(wxCommandEvent &event);
	VideoBox *vb = nullptr;

	DECLARE_EVENT_TABLE()
};
enum{
	Time_Copy = 4404,
	Time_Paste = 4405,
	//START_EDIT=(1<<22),
	//END_EDIT=(1<<23),
};


