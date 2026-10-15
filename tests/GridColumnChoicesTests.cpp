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

#include "check.h"
#include "../HikariSub/GridColumnChoices.h"

#include <cstring>
#include <set>
#include <string>

namespace {

const char *LabelOf(const std::vector<GridColumnChoice> &choices, int column)
{
	for (const GridColumnChoice &choice : choices) {
		if (choice.column == column)
			return choice.label;
	}
	return "";
}

// Every row of a dialog copies or pastes its own column under its own label.
bool RowsAreDistinct(const std::vector<GridColumnChoice> &choices)
{
	std::set<std::string> labels;
	std::set<int> columns;
	for (const GridColumnChoice &choice : choices) {
		labels.insert(choice.label);
		columns.insert(choice.column);
	}
	return labels.size() == choices.size() && columns.size() == choices.size();
}

} // namespace

TEST(GridColumnChoicesEndTimeIsLabelledEndTime)
{
	// SRT, MicroDVD and MPL2 used to label the end time "The starting time",
	// so both dialogs showed two identical rows.
	for (int format : { ASS, SRT, MDVD, MPL2 }) {
		CHECK(std::strcmp(LabelOf(PasteColumnChoices(format, false), END), "End time") == 0);
		CHECK(std::strcmp(LabelOf(PasteColumnChoices(format, true), END), "End time") == 0);
		CHECK(std::strcmp(LabelOf(CopyColumnChoices(format), END), "End time") == 0);
		CHECK(std::strcmp(LabelOf(CopyColumnChoices(format), START), "The starting time") == 0);
	}
}

TEST(GridColumnChoicesRowsAreDistinct)
{
	for (int format : { ASS, SRT, TMP, MDVD, MPL2 }) {
		CHECK(RowsAreDistinct(PasteColumnChoices(format, false)));
		CHECK(RowsAreDistinct(PasteColumnChoices(format, true)));
		CHECK(RowsAreDistinct(CopyColumnChoices(format)));
	}
}

TEST(GridColumnChoicesKeepTheColumnOrder)
{
	// The remembered choices are column flags, and the dialogs list the
	// columns in this order.
	auto columns = [](const std::vector<GridColumnChoice> &choices) {
		std::vector<int> result;
		for (const GridColumnChoice &choice : choices)
			result.push_back(choice.column);
		return result;
	};
	CHECK(columns(PasteColumnChoices(SRT, false)) == std::vector<int>({ START, END, TXT }));
	CHECK(columns(CopyColumnChoices(MDVD)) == std::vector<int>({ START, END, TXT }));
	CHECK(columns(PasteColumnChoices(TMP, false)) == std::vector<int>({ START, TXT }));
	CHECK(columns(PasteColumnChoices(ASS, false)) == std::vector<int>(
		{ LAYER, START, END, ACTOR, STYLE, MARGINL, MARGINR, MARGINV, EFFECT, TXT }));
	CHECK(columns(PasteColumnChoices(ASS, true)) == std::vector<int>(
		{ LAYER, START, END, ACTOR, STYLE, MARGINL, MARGINR, MARGINV, EFFECT, TXT, TXTTL }));
	CHECK(columns(CopyColumnChoices(ASS)) == std::vector<int>(
		{ LAYER, START, END, ACTOR, STYLE, MARGINL, MARGINR, MARGINV, EFFECT, TXT, TXTTL }));
	CHECK(std::strcmp(LabelOf(PasteColumnChoices(ASS, true), TXTTL), "Text into translation") == 0);
	CHECK(std::strcmp(LabelOf(CopyColumnChoices(ASS), TXTTL), "Text without tags") == 0);
}
