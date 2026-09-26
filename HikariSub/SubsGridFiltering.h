//  Copyright (c) 2012 - 2026, Marcin Drob

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
#include "SubsGrid.h"
#include <wx/dynarray.h>
#include <wx/arrstr.h>

class Dialogue;
//class SubsGrid;

class SubsGridFiltering
{
public:
	SubsGridFiltering(SubsGrid *_grid, int _activeLine);
	~SubsGridFiltering();

	void Filter(bool autoFiltering = false, bool removeFiltering = false);
	void FilterPartial(int from);
	void HideSelections();
	void MakeTree();
	void RemoveFiltering();

private:
	inline bool CheckHiding(Dialogue *dial, int i);
	void TurnOffFiltering();
	//FILTERING_CHANGE = 57
	void FilteringFinalize(int id = 57);
	SubsGrid *grid;
	bool Invert;
	//int activeLineDiff = 0;
	int activeLine;
	int filterBy = 0;
	int selectionsJ = 0;
	wxArrayInt keySelections;
	wxArrayString styles;
};

enum{
	FILTER_BY_STYLES = 1,
	FILTER_BY_SELECTIONS,
	FILTER_BY_DIALOGUES = 4,
	FILTER_BY_DOUBTFUL = 8,
	FILTER_BY_UNTRANSLATED = 16,
};
