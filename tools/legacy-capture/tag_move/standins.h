// Stand-ins for what EditBox::SetTextWithTags (EditBox.cpp:1809-1874, copied
// unchanged by extract_functions.py) reaches outside itself. The Line's text
// fields are legacy's own StoreTextHelper, copied unchanged from
// SubsDialogue.h; only the windows around them are stood in for, and each
// keeps the state the function sets so the probe can record it.
#pragma once

#include <wx/string.h>

#include <algorithm>
#include <string>
#include <vector>

// config.h:36
const wxString emptyString;

// Visuals.h:56-68 (only CROSS is compared against).
enum { CROSS = 0, CHANGEPOS, MOVE, SCALE, ROTATEZ, ROTATEXY, CLIPRECT, VECTORCLIP, VECTORDRAW, MOVEALL, ALL_TAGS };

// RendererVideo.h:43-49
enum PlaybackState : int { Playing, Paused, Stopped, None };

// SubtitlesProviderManager.h:25-31
enum { CLOSE_SUBTITLES, OPEN_DUMMY, OPEN_WHOLE_SUBTITLES, OPEN_HAS_OWN_TEXT };

// StoreTextHelper (SubsDialogue.h), unchanged.
#include "dialogue_helpers.inc"

// The Dialogue fields SetTextWithTags reads, and Copy (SubsDialogue.h:335-354).
class Dialogue {
public:
    StoreTextHelper Text, TextTl;
    Dialogue *Copy()
    {
        auto *d = new Dialogue;
        d->Text = static_cast<const wxString &>(Text);
        d->TextTl = static_cast<const wxString &>(TextTl);
        return d;
    }
};

// SubsFile::GetDialogue (SubsFile.cpp:427-433).
class SubsFile {
public:
    std::vector<Dialogue *> dialogues;
    Dialogue *GetDialogue(size_t i) { return i >= dialogues.size() ? nullptr : dialogues[i]; }
};

class SubsGrid {
public:
    bool hasTLMode = false;
    SubsFile *file = nullptr;
};

// The TextEditor calls it makes (DialogueTextEditor.cpp): SetTextS keeps the
// text and the modified flag and, with resetsel, selects 0..0 (135-155; the
// BIDI conversion is off in every call here); SetSelection clamps both ends
// to the text's length and keeps them as Cursor.x and Selend.x (2154-2164).
class TextEditor {
public:
    wxString MText;
    bool modified = false, shown = true, focused = false;
    int cursor = 0, selend = 0;
    int sets = 0;
    void SetTextS(const wxString &text, bool modif = false, bool resetsel = true, bool noevent = false,
                  bool BIDIConversion = false)
    {
        modified = modif;
        MText = text;
        ++sets;
        if (resetsel)
            SetSelection(0, 0);
    }
    void SetSelection(int start, int end, bool noEvent = false)
    {
        const int len = int(MText.length());
        cursor = std::clamp(end, 0, len);
        selend = std::clamp(start, 0, len);
    }
    void SetFocus() { focused = true; }
    bool IsShown() const { return shown; }
    bool IsModified() const { return modified; }
};

// MappedButton.h:75: the "Moving tags" toggle.
class ToggleButton {
public:
    bool value = false;
    bool GetValue() const { return value; }
};

// The VideoBox calls after `done:`, recorded in order.
class VideoBox {
public:
    PlaybackState state = None;
    std::vector<std::string> calls;
    PlaybackState GetState() { return state; }
    void OpenSubs(int mode) { calls.push_back(mode == OPEN_DUMMY ? "OpenSubs(OPEN_DUMMY)" : "OpenSubs(?)"); }
    void Render() { calls.push_back("Render"); }
};

class TabPanel {
public:
    VideoBox *video = nullptr;
};

// The EditBox members SetTextWithTags uses (EditBox.h:128-214).
class EditBox {
public:
    ToggleButton *AutoMoveTags = nullptr;
    TextEditor *TextEdit = nullptr;
    TextEditor *TextEditOrig = nullptr;
    Dialogue *line = nullptr;
    bool splittedTags = false;
    int Visual = CROSS;
    int currentLine = 0;
    SubsGrid *grid = nullptr;
    TabPanel *tab = nullptr;
    void SetTextWithTags(bool RefreshVideo = false);
};
