#pragma once

// The Grid clipboard's text forms (G2; legacy SubsGrid::CopyRows/OnPaste and
// Dialogue::SetRaw/GetRaw/GetCols/Convert with SubsTime at 20d647c4).
//
// Copy writes each Line in the legacy Dialogue::GetRaw form of the
// Document's format; copy columns writes the chosen fields (GetCols). Paste
// splits the text into lines, gathers numbered SRT blocks, reads each with the
// legacy SetRaw classification (ASS event, SRT, MicroDVD, MPL2, TMPlayer,
// non-dialogue or plain text) and converts it to the Document's format the
// way Dialogue::Convert does.

#include "hikari/core/document.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

// Legacy column bits (SubsDialogue.h).
namespace column {
inline constexpr int Layer = 1, Start = 2, End = 4, Style = 8, Actor = 16, MarginLeft = 32, MarginRight = 64,
                     MarginVertical = 128, Effect = 256, Text = 1024, Translation = 2048;
// Copy columns uses the translation bit for "Text without tags".
inline constexpr int TextWithoutTags = Translation;
} // namespace column

// GRID_COPY for these Lines, in this order. SRT numbers each cue by its
// Document row. In translation mode a Line with a translation is copied with
// the translation as its text. Without `numberSrtCues` the Lines are
// written by Dialogue::GetRaw alone (select lines' Copy and Cut).
std::u8string clipboardRows(const Document &document, const std::vector<LineId> &lines, bool translationMode,
                            bool numberSrtCues = true);
// GRID_COPY_COLUMNS: the chosen fields of each Line, one line each.
std::u8string clipboardColumns(const Document &document, const std::vector<LineId> &lines, int columns,
                               bool translationMode);

// The conversion options legacy reads (CONVERT_STYLE, the tags inserted at
// the start of converted lines, and the MicroDVD frame rate, SubsTime::FPS).
struct PasteConversion {
    std::u8string style = u8"Default";
    std::u8string prefix;
    float fps = 23.976f;
};

// GRID_PASTE / GRID_PASTE_COLUMNS text, read and converted to `format`: one
// Line per entry with field values only (no source span); a MicroDVD Line
// keeps its frames in startFrame/endFrame. `intoTranslation` (the
// paste-columns "Text into translation" choice) copies the text into the
// translation before conversion, as legacy does.
std::vector<LineRecord> parseClipboardRows(std::u8string_view text, SubtitleFormat format,
                                           const PasteConversion &conversion = {}, bool intoTranslation = false);

// Legacy Dialogue(raw) then Convert(format), as GRID_PASTE_TRANSLATION reads
// each entry of the chosen file: the Line it gives, its text in `format`.
LineRecord dialogueFromRaw(std::u8string_view raw, SubtitleFormat format, const PasteConversion &conversion = {});

// F1 (find and replace in files, legacy FindReplaceInFiles and
// ReplaceCheckedInSubs): one line of a subtitle file read as legacy
// Dialogue(raw). A ";..." or lone "{...}" line is a non-dialogue comment
// whose text is the whole line.
struct RawDialogueFields {
    bool comment = false;
    std::u8string style;
    std::u8string actor;
    std::u8string effect;
    std::u8string text;
};
RawDialogueFields rawDialogueFields(std::u8string_view raw);
// SetTextElement(column, value) then GetRaw: the line in its own format,
// "\r\n" included (a plain-text line comes back as an ASS Dialogue line, as
// legacy writes Format 0 Lines). `column` is column::Text, Style, Actor or Effect.
std::u8string rawDialogueWithField(std::u8string_view raw, int column, std::u8string_view value);

} // namespace hikari::core
