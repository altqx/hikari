#pragma once

#include "hikari/core/document.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

namespace column {
inline constexpr int Layer = 1, Start = 2, End = 4, Style = 8, Actor = 16, MarginLeft = 32, MarginRight = 64,
                     MarginVertical = 128, Effect = 256, Text = 1024, Translation = 2048;
// Copy columns uses the translation bit for "Text without tags".
inline constexpr int TextWithoutTags = Translation;
}

// In the given order. With `numberSrtCues` SRT numbers each cue by its
// Document row. In translation mode a Line's translation, if any, is copied
// as its text.
std::u8string clipboardRows(const Document &document, const std::vector<LineId> &lines, bool translationMode,
                            bool numberSrtCues = true);
// One line per Line.
std::u8string clipboardColumns(const Document &document, const std::vector<LineId> &lines, int columns,
                               bool translationMode);

struct PasteConversion {
    std::u8string style = u8"Default";
    std::u8string prefix; // tags inserted at the start of converted lines
    float fps = 23.976f;  // MicroDVD frame rate
};

// Reads ASS events, numbered SRT blocks, MicroDVD, MPL2, TMPlayer,
// non-dialogue and plain text. One Line per entry, field values only (no
// source span); a MicroDVD Line keeps its frames in startFrame/endFrame.
// `intoTranslation` copies the text into the translation before conversion.
std::vector<LineRecord> parseClipboardRows(std::u8string_view text, SubtitleFormat format,
                                           const PasteConversion &conversion = {}, bool intoTranslation = false);

// One raw entry as a Line, its text converted to `format`.
LineRecord dialogueFromRaw(std::u8string_view raw, SubtitleFormat format, const PasteConversion &conversion = {});

// A ";..." or lone "{...}" line is a non-dialogue comment whose text is the whole line.
struct RawDialogueFields {
    bool comment = false;
    std::u8string style;
    std::u8string actor;
    std::u8string effect;
    std::u8string text;
};
RawDialogueFields rawDialogueFields(std::u8string_view raw);
// The line in its own format, "\r\n" included; a plain-text line comes back
// as an ASS Dialogue line. `column` is column::Text, Style, Actor or Effect.
std::u8string rawDialogueWithField(std::u8string_view raw, int column, std::u8string_view value);

// No conversion. `format`: 0 plain text, 1 ASS, 2 SRT, 3 TMPlayer, 4 MicroDVD,
// 5 MPL2; `raw`: the line in that format, "\r\n" included.
struct LegacyDialogue {
    LineRecord line;
    // MUST KILL: format is a raw int code, not SubtitleFormat
    int format = 1;
    bool nonDialogue = false;
    std::u8string raw;
};
LegacyDialogue legacyDialogue(std::u8string_view raw);

}
