//  Copyright (c) 2018 - 2026, Marcin Drob

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
#include "HikariTextCtrl.h"
#include "HikariCheckBox.h"

class FPSDialog : public HikariDialog
{
public:
	FPSDialog(wxWindow *parent);
	virtual ~FPSDialog(){};
	void OkClick(wxCommandEvent &evt);
	double ofps, nfps;
	HikariChoice *oldfps;
	HikariChoice *newfps;
};

class TreeDialog : public HikariDialog
{
public:
	TreeDialog(wxWindow *parent, const wxString & currentName);
	virtual ~TreeDialog(){}
	wxString GetDescription();

private:
	void OkClick(wxCommandEvent &evt);
	HikariTextCtrl *treeDescription;
};

class SwapPropertiesDialog :public HikariDialog
{
public:
	SwapPropertiesDialog(wxWindow* parent);
	virtual ~SwapPropertiesDialog() {};
	void SaveValues();

private:
	HikariCheckBox* fields[6];
};