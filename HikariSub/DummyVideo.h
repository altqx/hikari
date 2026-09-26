//  Copyright (c) 2021 - 2026, Marcin Drob

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
#include "ListControls.h"
#include "HikariCheckBox.h"
#include "NumCtrl.h"
#include "ColorPicker.h"
#include "TimeCtrl.h"
#include "HikariStaticText.h"

class DummyVideo : public HikariDialog
{
public:
	DummyVideo(wxWindow* parent);
	virtual ~DummyVideo() {};
	wxString GetDummyText();
private:
	void OnResolutionChoose(wxCommandEvent& evt);
	HikariChoice* videoResolution;
	NumCtrl* videoResolutionWidth;
	NumCtrl* videoResolutionHeight;
	ButtonColorPicker* color;
	HikariCheckBox* pattern;
	HikariChoice* frameRate;
	TimeCtrl* duration;
	HikariStaticText* frameDuration;

	enum {
		ID_VIDEO_RESOLUTION = 5678
	};
};
