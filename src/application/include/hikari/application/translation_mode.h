#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

// Refused when the mode is already on or the format is not ASS; adds a Script
// Info section when missing.
std::expected<void, CommandRefusal> turnOnTranslationModeStep(EditSession &session);

// Flips the active and selected Lines in one step; a pending draft on one of
// them commits as its own step first. Refused outside translation mode,
// without any Line, or when that draft can't be committed.
std::expected<void, CommandRefusal> toggleUnconfirmed(EditSession &session);

// Override blocks go to `translation`, the rest to `original`; `caret` is in
// UTF-16 units of `translation`.
struct MovedTags {
    std::u8string original;
    std::u8string translation;
    std::size_t caret = 0;
};
// nullopt when the text holds no '}'.
std::optional<MovedTags> moveTagsFromOriginal(std::u8string_view text);

// Whether the Grid shows "Original text" and "Translation" in place of "Text";
// Grid state, not a Document value.
class OriginalColumns {
public:
    // Call every time the Grid shows the Document. Returns shown().
    bool observe(const core::Document &document, bool showOriginalSetting);
    // Shows the columns only when `showOriginalSetting` is set.
    void turnedOn(const core::Document &after, bool showOriginalSetting);
    void turnedOff(const core::Document &after);
    void pasted(const core::Document &after);
    bool shown() const { return m_shown; }

private:
    void see(const core::Document &document);
    bool m_seen = false;
    std::optional<std::u8string> m_tlMode;
    bool m_shown = false;
};

} // namespace hikari::application
