// Stand-in for the legacy SubsDialogue.h, enough for KaraokeSplitting.cpp:
// the karaoke code reads a Line's Text, TextTl, Start and End only. The
// probe compiles a copy of the legacy KaraokeSplitting.cpp next to these
// headers, so its quoted includes find them instead of the GUI headers.
#pragma once

#include <wx/string.h>

#define ZEROIT(a) ((a/10)*10)

const wxString emptyString;

struct SubsTime {
    int mstime = 0;
};

class Dialogue {
public:
    wxString Text, TextTl;
    SubsTime Start, End;
};
