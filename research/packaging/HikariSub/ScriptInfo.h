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


#include "HikariDialog.h"
#include "NumCtrl.h"
#include "MappedButton.h"
#include "ListControls.h"
#include "HikariCheckBox.h"


class ScriptInfo : public HikariDialog
{
public:

	ScriptInfo(wxWindow* parent, int w, int h);
	virtual ~ScriptInfo();

	NumCtrl* height;
	NumCtrl* layoutHeight;
	HikariTextCtrl* script;
	NumCtrl* width;
	NumCtrl* layoutWidth;
	HikariTextCtrl* update;
	MappedButton* save;
	HikariChoice* wrapstyle;
	HikariChoice* matrix;
	HikariCheckBox* scaleBorderAndShadow;
	HikariChoice* collision;
	HikariTextCtrl* editing;
	HikariTextCtrl* title;
	MappedButton* cancel;
	HikariTextCtrl* timing;
	HikariTextCtrl* translation;
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


