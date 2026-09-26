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

#include <wx/window.h>
#include <wx/statbox.h>
#include <wx/sizer.h>
#include <wx/dc.h>

struct tagRECT;


class HikariStaticBox : public
#ifdef _WIN32
	wxStaticBox
#else
	wxWindow
#endif
{
public:
	HikariStaticBox(wxWindow *parent, const wxString& label);
	HikariStaticBox(wxWindow *parent, int numLabels, wxString* labels);
	virtual ~HikariStaticBox(){};
	wxSize CalcBorders();
	bool Enable(bool enable = true);
private:
	void PaintForeground(wxDC& dc, const tagRECT& rc);
	wxArrayString labels;
	int heightText;
};

// wx 3.3 changed wxSizer::Detach() to take wxWindowBase*; 3.2 takes wxWindow*.
#if wxCHECK_VERSION(3, 3, 0)
using HikariSizerDetachArg = wxWindowBase;
#else
using HikariSizerDetachArg = wxWindow;
#endif

class HikariStaticBoxSizer : public wxBoxSizer
{
public:
	HikariStaticBoxSizer(int orient, wxWindow *parent, const wxString& _label);
	HikariStaticBoxSizer(int orient, wxWindow *parent, int n, wxString * _labels);
	virtual ~HikariStaticBoxSizer();
	void ShowItems( bool show ) override;
	bool Enable(bool enable);
	// Without an exact match this stopped overriding wxSizer::Detach, so
	// ~wxWindowBase never cleared box and the destructor deleted it twice.
	bool Detach( HikariSizerDetachArg *window ) override;

private:
	// wxBoxSizer overrides RepositionChildren(), so its RecalcSizes()
	// compatibility shim never runs and an override of it is dead code.
	void RepositionChildren(const wxSize& minSize) override;
	wxSize CalcMin() override;
	HikariStaticBox *box;
};
