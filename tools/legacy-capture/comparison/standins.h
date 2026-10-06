// Stand-ins for what SubsGrid::SubsComparison and SubsGrid::CompareTexts
// (SubsGridBase.cpp:1737-1884, copied unchanged by extract_functions.py) reach
// outside themselves. The Line's field types are legacy's own: StoreTextHelper
// and Visibility are copied unchanged from SubsDialogue.h, SubsTime is the
// legacy SubsTime.cpp, HikariLog is LogHandler.h's, and compareData is copied
// unchanged from SubsGrid.h. Only the containers around them are stood in for.
#pragma once

#include <wx/arrstr.h>
#include <wx/dynarray.h>
#include <wx/string.h>

#include <atomic>
#include <cstddef>
#include <limits>
#include <set>
#include <vector>

#include "LogHandler.h"
#include "SubsTime.h"

// config.h:36
const wxString emptyString;

// SubsDialogue.h:45-47 (the visibility values of the anonymous enum).
enum { NOT_VISIBLE = 0, VISIBLE, VISIBLE_BLOCK };

// Notebook.h:140-144
enum {
    COMPARE_BY_TIMES = 1,
    COMPARE_BY_STYLES,
    COMPARE_BY_CHOSEN_STYLES = 4,
    COMPARE_BY_VISIBLE = 8,
    COMPARE_BY_SELECTIONS = 16
};

// StoreTextHelper and Visibility (SubsDialogue.h), unchanged.
#include "dialogue_helpers.inc"

// The Dialogue fields SubsComparison reads (SubsDialogue.h:335-341).
class Dialogue {
public:
    StoreTextHelper Style, Text, TextTl;
    SubsTime Start, End;
    Visibility isVisible;
};

// SubsFile's GetCount, GetDialogue and IsSelected (SubsFile.cpp:349-352,
// 427-433, 565-568) over the dialogues and the selection set they read.
class SubsFile {
public:
    std::vector<Dialogue *> dialogues;
    std::set<int> Selections;
    size_t GetCount() { return dialogues.size(); }
    Dialogue *GetDialogue(size_t i) { return i >= dialogues.size() ? nullptr : dialogues[i]; }
    bool IsSelected(size_t i) { return Selections.find(int(i)) != Selections.end(); }
};

// Options.GetInt(SUBS_COMPARISON_TYPE)
enum { SUBS_COMPARISON_TYPE };
struct ProbeOptions {
    int comparisonType = 0;
    int GetInt(int) const { return comparisonType; }
};
extern ProbeOptions Options;

// compareData (SubsGrid.h:67-78), unchanged.
#include "compare_data.inc"

// The SubsGrid members SubsComparison uses (SubsGrid.h:195-223).
class SubsGrid {
public:
    bool hasTLMode = false;
    std::vector<compareData> *Comparison = nullptr;
    SubsFile *file = nullptr;
    void Refresh(bool) {}
    static SubsGrid *CG1;
    static SubsGrid *CG2;
    static void SubsComparison();
    static wxArrayString compareStyles;
    static void CompareTexts(compareData &firstTable, compareData &secondTable, const wxString &first,
                             const wxString &second);
};
