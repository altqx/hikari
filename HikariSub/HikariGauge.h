//  Copyright (c) 2020 - 2026, Marcin Drob
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
#include <wx/gauge.h>

class HikariGauge : public wxWindow
{
public:
	HikariGauge(wxWindow *parent, int id, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = 0);
	virtual ~HikariGauge(){};
	void SetValue(int parcent);
private:

	void OnPaint(wxPaintEvent &evt);
	void OnSize(wxSizeEvent &evt);
	int numParcent = 0;
};