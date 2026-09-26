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


#include "KaiDialog.h"
#include "NumCtrl.h"
#include "MappedButton.h"
#include "ListControls.h"
#include "KaiCheckBox.h"


class ScriptInfo : public KaiDialog
{
public:

	ScriptInfo(wxWindow* parent, int w, int h);
	virtual ~ScriptInfo();

	NumCtrl* height;
	NumCtrl* layoutHeight;
	KaiTextCtrl* script;
	NumCtrl* width;
	NumCtrl* layoutWidth;
	KaiTextCtrl* update;
	MappedButton* save;
	KaiChoice* wrapstyle;
	KaiChoice* matrix;
	KaiCheckBox* scaleBorderAndShadow;
	KaiChoice* collision;
	KaiTextCtrl* editing;
	KaiTextCtrl* title;
	MappedButton* cancel;
	KaiTextCtrl* timing;
	KaiTextCtrl* translation;
	MappedButton* resolutionFromVideo;
	MappedButton* layoutFromVideo;
	ToggleButton* linkResolutions;

	void DoTooltips();
private:
	wxSize res;
	void OnVideoRes(wxCommandEvent& event);
	void OnLayoutRes(wxCommandEvent& event);
	void OnResolutionLink(wxCommandEvent& event);
};


