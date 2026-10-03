#pragma once

// E3: translation operations in the Grid (legacy SubsGrid::OnPasteTextTl,
// SetTlMode and MoveTextTL with TLDialog at 20d647c4).

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/clipboard_rows.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// SubsGrid::SetTlMode(true) on a Document: without "TLMode" in Script Info
// a copy of the Default Style (the first Style when there is none) named
// TLmode, TLmode1, ... (the first free name) with alignment 8 is added and
// named in "TLMode Style"; then "TLMode: Yes". False without a Script Info
// section.
bool turnOnTranslationMode(core::Document &document);

// GRID_PASTE_TRANSLATION ("Pasting translation"): each entry of the chosen
// file becomes the translation of the next shown Line, from the first; once
// they run out, new Lines are appended (zero times, the Default Style, the
// entry as the translation and no text). The text is read as legacy reads
// it on Windows, CRLF as LF. An ".srt" file is read as numbered blocks, and
// as in legacy its last block is never pasted; an ".ass" file
// gives its Dialogue lines; other files give every non-empty line.
// Translation mode is turned on and the original shown ("TLMode Showtl:
// Yes"). Refused for the line formats.
std::expected<void, CommandRefusal> pasteTranslation(EditSession &session, std::u8string_view fileText,
                                                     std::u8string_view extension, const LineVisible &visible = {},
                                                     const core::PasteConversion &conversion = {});

// GRID_TRANSLATION_DIALOG ("Dialogue shifting window", TLDialog), legacy
// MoveTextTL modes. The count is 1, or with several Lines selected the row
// distance between the first two. Only with translation mode on and the
// original shown.
enum class TranslationMove {
    DeleteTranslationLine = 0, // "Delete line" (Translation): the translation moves up
    JoinTranslation = 1,       // "Join lines" (Translation): joined with the next, the rest moves up
    AddOriginalLine = 2,       // "Add line" (Original): blank Lines before, the original moves down
    AddTranslationLine = 3,    // "Add line" (Translation): the translation moves down
    JoinOriginal = 4,          // "Join lines" (Original): joined with the next, the original moves up
    DeleteOriginalLine = 5,    // "Delete line" (Original): the original moves up
};
std::expected<void, CommandRefusal> moveTranslation(EditSession &session, TranslationMove move,
                                                    const LineVisible &visible = {});

} // namespace hikari::application
