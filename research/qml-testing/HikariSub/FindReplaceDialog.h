//  Copyright (c) 2018-2026, Marcin Drob

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
#include "HikariRadioButton.h"
#include "MappedButton.h"
#include "HikariTabBar.h"
#include "HikariStaticText.h"

class FindReplace;
class HikariSubFrame;

class TabWindow : public wxWindow
{
	friend class FindReplace;
public:
	TabWindow(wxWindow *parent, int id, int tabNum, FindReplace * FR);
	virtual ~TabWindow(){};
	void SaveValues();
	void SetValues();

	void OnRecheck(wxCommandEvent& event);
	void Reset(wxCommandEvent& evt);
	void OnStylesChoose(wxCommandEvent& event);
	HikariChoice* FindText;
	HikariChoice* ReplaceText = nullptr;
	HikariChoice* FindInSubsPattern = nullptr;
	HikariChoice* FindInSubsPath = nullptr;
	HikariRadioButton* CollumnText;
	HikariRadioButton* CollumnStyle;
	HikariRadioButton* CollumnActor;
	HikariRadioButton* CollumnEffect;
	HikariRadioButton* AllLines = nullptr;
	HikariRadioButton* SelectedLines = nullptr;
	HikariRadioButton* FromSelection = nullptr;
	HikariTextCtrl *ChoosenStyleText = nullptr;
	HikariCheckBox* MatchCase;
	HikariCheckBox* RegEx;
	HikariCheckBox* StartLine;
	HikariCheckBox* EndLine;
	HikariCheckBox* UseComments;
	HikariCheckBox* OnlyText;
	HikariCheckBox* OnlyTags;
	HikariCheckBox *SeekInSubFolders = nullptr;
	HikariCheckBox *SeekInHiddenFolders = nullptr;
	FindReplace *FR;
	int windowType = 0;
};

class FindReplaceDialog : public HikariDialog
{
	friend class FindReplace;
public:
	FindReplaceDialog(HikariSubFrame *Hikari, int whichWindow);
	virtual ~FindReplaceDialog();
	void ShowDialog(int whichWindow);
	void SaveOptions();
	void Reset();
	TabWindow *GetTab();
	void FindNext();
private:
	void OnActivate(wxActivateEvent& event);
	void OnEnterConfirm(wxCommandEvent& event);
	void SetSelection(TabWindow *tab);
	FindReplace *FR = nullptr;
	HikariSubFrame *Hikari = nullptr;
	HikariTabBar * findReplaceTabs = nullptr;
	int lastFocusedId = -1;
};

enum{
	WINDOW_FIND = 0,
	WINDOW_REPLACE,
	WINDOW_FIND_IN_SUBS,
	ID_BUTTON_REPLACE = 13737,
	ID_BUTTON_REPLACE_ALL,
	ID_BUTTON_REPLACE_IN_ALL_OPENED_SUBS,
	ID_BUTTON_FIND,
	ID_BUTTON_FIND_IN_ALL_OPENED_SUBS,
	ID_BUTTON_FIND_ALL_IN_CURRENT_SUBS,
	ID_BUTTON_FIND_IN_SUBS,
	ID_BUTTON_REPLACE_IN_SUBS,
	ID_BUTTON_CLOSE,
	ID_BUTTON_CHOOSE_STYLE,
	ID_CHOOSEN_STYLE_TEXT,
	ID_FIND_TEXT,
	ID_REPLACE_TEXT,
	ID_START_OF_LINE,
	ID_END_OF_LINE,
	ID_ONLY_TEXT,
	ID_ONLY_TAGS,
	ID_ENTER_CONFIRM
};