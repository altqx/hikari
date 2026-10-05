#pragma once

// E5: translation mode controls (legacy EditBox "Translator mode" check box,
// "Not confirmed" and "Moving tags" toggle buttons, SubsGrid::showOriginal
// and TL_MODE_HIDE_ORIGINAL_ON_VIDEO at 20d647c4). Turning the mode on as
// E3's paste does is grid_translation's turnOnTranslationMode; turning it off
// is its turnOffTranslationMode.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

// EditBox::OnTlMode (EditBox.cpp:1072-1078) turning the check box on:
// SubsGrid::SetTlMode(true) (SubsGridBase.cpp:1311-1334) as one "Turning on
// translator mode" step (GRID_TURN_ON_TLMODE, SubsFile.cpp:219). Refused when
// translation mode is already on, for the line formats (legacy enables the
// check box for ASS only), or without a Script Info section.
std::expected<void, CommandRefusal> turnOnTranslationModeStep(EditSession &session);

// EditBox::OnDoubtfulTl (EditBox.cpp:1946-1962), the "Not confirmed" button
// and EDITBOX_SET_DOUBTFUL: the active Line's and every selected Line's
// Unconfirmed flips (Dialogue::ChangeState(4) toggles State bit 4), as one
// "Mark unconfirmed" step. Legacy flips the editor's copy of the active Line
// and the selected Lines; an active Line outside the selection reaches the
// Document at its next commit there, here at once. Refused outside
// translation mode (legacy rings the bell) and without any Line.
std::expected<void, CommandRefusal> toggleUnconfirmed(EditSession &session);

// EditBox::SetTextWithTags with "Moving tags" (EditBox.cpp:1809-1858): an
// untranslated Line's text holding a '}' is shown split, its override blocks
// in the Translated field and the rest in the Original field. `caret` is
// where the Translated field's caret goes (after a leading block), in UTF-16
// units of `translation`. nullopt when the text holds no '}' (shown whole).
struct MovedTags {
    std::u8string original;
    std::u8string translation;
    std::size_t caret = 0;
};
std::optional<MovedTags> moveTagsFromOriginal(std::u8string_view text);

// SubsGrid::showOriginal (SubsGrid.h:196): whether the Grid shows the
// "Original text" and "Translation" columns in place of "Text". It is the
// Grid's own state, not a Document value, and changes only at these moments.
class OriginalColumns {
public:
    // Every time the Grid shows the Document. The first time is
    // SubsGrid::Clearing then LoadSubtitles (SubsGridBase.cpp:125,
    // 1214-1216): "TLMode: Yes" and either "TLMode Showtl: Yes" or
    // TL_MODE_SHOW_ORIGINAL. Afterwards, when TLMode has another value than
    // the last one seen (DoUndo, SubsGridBase.cpp:994-998): "TLMode Showtl:
    // Yes", or "TLMode: Yes" with TL_MODE_SHOW_ORIGINAL. Returns shown().
    bool observe(const core::Document &document, bool showOriginalSetting);
    // SetTlMode(true) (SubsGridBase.cpp:1331): on with TL_MODE_SHOW_ORIGINAL,
    // otherwise unchanged.
    void turnedOn(const core::Document &after, bool showOriginalSetting);
    // SetTlMode(false) (SubsGridBase.cpp:1371), also from Save translation.
    void turnedOff(const core::Document &after);
    // OnPasteTextTl (SubsGrid.cpp:1026-1027).
    void pasted(const core::Document &after);
    bool shown() const { return m_shown; }

private:
    void see(const core::Document &document);
    bool m_seen = false;
    std::optional<std::u8string> m_tlMode;
    bool m_shown = false;
};

} // namespace hikari::application
