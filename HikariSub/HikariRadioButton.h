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

#include "HikariCheckBox.h"
#include <vector>

class HikariRadioButton : public HikariCheckBox
{
public:
	HikariRadioButton(wxWindow *parent, int id, const wxString& label,
			 const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);
	virtual ~HikariRadioButton(){};
	void SetValue(bool value);
private:
	
	void OnMouseLeft(wxMouseEvent &evt);
	void DeselectRest();
	void SelectNext(bool last);
	void SelectPrev(bool first);
	//bool hasGroup;

	wxDECLARE_ABSTRACT_CLASS(HikariRadioButton);
};
class HikariStaticBoxSizer;

class HikariRadioBox : public wxWindow
{
public:
	HikariRadioBox(wxWindow *parent, int id, const wxString& label,
			 const wxPoint& pos, const wxSize& size, const wxArrayString &names, int spacing=1, long style = wxVERTICAL);
	virtual ~HikariRadioBox(){};
	int GetSelection();
	void SetSelection(int sel);
	bool Enable(bool enable=true);
	void SetFocus();
private:
	//void OnNavigation(wxNavigationKeyEvent& evt);
	int selected;
	std::vector< HikariRadioButton*> buttons;
	HikariStaticBoxSizer *box;
	wxDECLARE_ABSTRACT_CLASS(HikariRadioBox);
};

enum{
	ID_ACCEL_LEFT=19876,
	ID_ACCEL_RIGHT,
};

