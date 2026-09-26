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

#include "HikariListCtrl.h"
#include "MappedButton.h"
#include "HikariDialog.h"


class Stylelistbox: public HikariDialog
{
	public:

		Stylelistbox(wxWindow* parent, bool styles = true, int numelem = 0, 
			wxString *arr = 0, const wxPoint& pos = wxDefaultPosition, int style = 0);
		Stylelistbox(wxWindow* parent, const wxArrayString & arr, bool styles = true, 
			const wxPoint& pos = wxDefaultPosition, int style = 0);
		virtual ~Stylelistbox();

		
		MappedButton* OK;
		HikariListCtrl* CheckListBox;
		MappedButton* Cancel;
		
};

class CustomCheckListBox : public HikariDialog
{
public:

	CustomCheckListBox(wxWindow* parent, const wxArrayString &listElems, 
		const wxString &title, const wxPoint& pos = wxDefaultPosition, int style = 0);
	virtual ~CustomCheckListBox(){};

	void GetCheckedElements(wxArrayString &checkedElements);

	MappedButton* OK;
	HikariListCtrl* CheckListBox;
	MappedButton* Cancel;

};

wxString GetCheckedElements(wxWindow *parent);

class HikariListBox : public HikariDialog
{
public:
	HikariListBox(wxWindow *parent, const wxArrayString &list, const wxString &title, bool centerOnParent = false);
	virtual ~HikariListBox(){};
	HikariListCtrl *list;
	wxString GetSelection() const{return result;};
	int GetIntSelection() {return selection;}
private:
	void OnDoubleClick(wxCommandEvent& evt);
	void OnOKClick(wxCommandEvent& evt);
	wxString result;
	int selection;
};


