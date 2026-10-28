//  Copyright (c) 2016 - 2026, Marcin Drob
//  Copyright (c) 2026, altqx

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
#include "HikariCheckBox.h"
#include "config.h"
#include <wx/string.h>
#include <wx/timer.h>

class HikariMessageDialog : public HikariDialog
{
public:
	HikariMessageDialog(wxWindow *parent, const wxString& msg, const wxString &caption, long elems = wxOK, const wxPoint &pos = wxDefaultPosition, long buttonWithFocus = -1);
	virtual ~HikariMessageDialog(){}
	void SetOkLabel(const wxString &label);
	void SetYesLabel(const wxString &label);
	void SetNoLabel(const wxString &label);
	void SetHelpLabel(const wxString &label);
	HikariCheckBox *kcb = nullptr;
	DialogSizer* sizer2 = nullptr;
	wxTimer automationDismissTimer;

};

//position set 0 to center position -1 not works
int HikariMessageBox(const wxString& msg, const wxString &caption = emptyString, long elems = wxOK, wxWindow *parent = 0, const wxPoint &pos = wxDefaultPosition, long buttonWithFocus = -1);

#define wxYES_TO_ALL 64
#define ASK_ONCE 0x40000000