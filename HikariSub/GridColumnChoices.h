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

#include "DialogueColumns.h"
#include "SubsFormats.h"
#include <vector>

// wxTRANSLATE only marks the labels for the catalog; the tests build this
// header without wxWidgets.
#ifndef wxTRANSLATE
#define wxTRANSLATE(str) str
#endif

// A row of the Copy columns or Paste columns dialog: the label it shows,
// untranslated, and the column it copies or pastes.
struct GridColumnChoice {
	const char *label;
	int column;
};

// The rows of the Paste columns dialog for a subtitle format, in order.
// In translation mode the text pastes into the original or the translation.
inline std::vector<GridColumnChoice> PasteColumnChoices(int subsFormat, bool hasTLMode)
{
	if (subsFormat == ASS) {
		std::vector<GridColumnChoice> choices = {
			{ wxTRANSLATE("Layer"), LAYER },
			{ wxTRANSLATE("The starting time"), START },
			{ wxTRANSLATE("End time"), END },
			{ wxTRANSLATE("Actor"), ACTOR },
			{ wxTRANSLATE("Styles"), STYLE },
			{ wxTRANSLATE("Left margin"), MARGINL },
			{ wxTRANSLATE("Right margin"), MARGINR },
			{ wxTRANSLATE("Vertical margin"), MARGINV },
			{ wxTRANSLATE("Effect"), EFFECT },
		};
		if (hasTLMode) {
			choices.push_back({ wxTRANSLATE("Text into original"), TXT });
			choices.push_back({ wxTRANSLATE("Text into translation"), TXTTL });
		}
		else {
			choices.push_back({ wxTRANSLATE("Text"), TXT });
		}
		return choices;
	}
	if (subsFormat == TMP) {
		return {
			{ wxTRANSLATE("The starting time"), START },
			{ wxTRANSLATE("Text"), TXT },
		};
	}
	return {
		{ wxTRANSLATE("The starting time"), START },
		{ wxTRANSLATE("End time"), END },
		{ wxTRANSLATE("Text"), TXT },
	};
}

// The rows of the Copy columns dialog for a subtitle format, in order.
inline std::vector<GridColumnChoice> CopyColumnChoices(int subsFormat)
{
	if (subsFormat == ASS) {
		return {
			{ wxTRANSLATE("Layer"), LAYER },
			{ wxTRANSLATE("The starting time"), START },
			{ wxTRANSLATE("End time"), END },
			{ wxTRANSLATE("Actor"), ACTOR },
			{ wxTRANSLATE("Styles"), STYLE },
			{ wxTRANSLATE("Left margin"), MARGINL },
			{ wxTRANSLATE("Right margin"), MARGINR },
			{ wxTRANSLATE("Vertical margin"), MARGINV },
			{ wxTRANSLATE("Effect"), EFFECT },
			{ wxTRANSLATE("Text"), TXT },
			{ wxTRANSLATE("Text without tags"), TXTTL },
		};
	}
	if (subsFormat == TMP) {
		return {
			{ wxTRANSLATE("The starting time"), START },
			{ wxTRANSLATE("Text"), TXT },
		};
	}
	return {
		{ wxTRANSLATE("The starting time"), START },
		{ wxTRANSLATE("End time"), END },
		{ wxTRANSLATE("Text"), TXT },
	};
}
