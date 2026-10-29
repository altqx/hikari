#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/clipboard_rows.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Without "TLMode" in Script Info, adds a copy of the Default Style (else the
// first) under the first free name TLmode, TLmode1, ... with alignment 8,
// names it in "TLMode Style" and sets "TLMode: Yes". False for non-ASS formats.
bool turnOnTranslationMode(core::Document &document);

// Removes "TLMode" and the Style named in "TLMode Style"; each translated Line
// takes its translation as its text; Unconfirmed is cleared. Refused unless
// translation mode is on.
std::expected<void, CommandRefusal> turnOffTranslationMode(EditSession &session);

// Each entry becomes the translation of the next shown Line; leftover entries
// become new Lines. On Linux `fileText` keeps each line's "\r", so an ".srt"
// file's last block is pasted only when a blank "\r" line follows it. Turns
// translation mode on with the original shown; refused for non-ASS formats.
std::expected<void, CommandRefusal> pasteTranslation(EditSession &session, std::u8string_view fileText,
                                                     std::u8string_view extension, const LineVisible &visible = {},
                                                     const core::PasteConversion &conversion = {});

// The count is 1, or with several Lines selected the row distance between the
// first two. Only with translation mode on and the original shown.
enum class TranslationMove {
    DeleteTranslationLine = 0, // the translation moves up
    JoinTranslation = 1,       // joined with the next, the rest moves up
    AddOriginalLine = 2,       // blank Lines before, the original moves down
    AddTranslationLine = 3,    // the translation moves down
    JoinOriginal = 4,          // joined with the next, the original moves up
    DeleteOriginalLine = 5,    // the original moves up
};
std::expected<void, CommandRefusal> moveTranslation(EditSession &session, TranslationMove move,
                                                    const LineVisible &visible = {});

} // namespace hikari::application
