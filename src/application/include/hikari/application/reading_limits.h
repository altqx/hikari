#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/settings.h"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace hikari::core {
class Document;
}

namespace hikari::application {

// The one reading-speed and line-length limit. The Grid, the Line editor, AI Shorten
// and the problems list read it here rather than keeping their own numbers.
struct ReadingLimits {
    int cpsWarning = 15;
    int cpsError = 30;
    int lineLength = 43;
    int maxLines = 2;

    friend bool operator==(const ReadingLimits &, const ReadingLimits &) = default;
};

// 0 up to the warning level, rising to 1 at the error level: Aegisub 3.5's Grid CPS tint.
double cpsSeverity(const ReadingLimits &limits, int cps);

ReadingLimits profileReadingLimits(const Settings &settings);

// The document's "Hikari CPS Warning", "Hikari CPS Error", "Hikari Line Length" and
// "Hikari Max Lines" Script Info keys. A key that is missing or isn't a positive integer is empty.
struct ScriptReadingLimits {
    std::optional<int> cpsWarning, cpsError, lineLength, maxLines;

    bool empty() const { return !cpsWarning && !cpsError && !lineLength && !maxLines; }
    // Each value the document holds replaces the profile's.
    ReadingLimits over(const ReadingLimits &profile) const;

    friend bool operator==(const ScriptReadingLimits &, const ScriptReadingLimits &) = default;
};

ScriptReadingLimits scriptReadingLimits(const core::Document &document);

inline ReadingLimits readingLimits(const core::Document &document, const ReadingLimits &profile)
{
    return scriptReadingLimits(document).over(profile);
}

struct ScriptInfoWrite {
    std::u8string key;
    std::optional<std::u8string> value; // nothing removes the key
};

// Sets all four keys, or with nothing removes them.
std::vector<ScriptInfoWrite> readingLimitsInfo(const std::optional<ReadingLimits> &limits);

// One undo step ("Changing the subtitle header"); none when the document already holds that.
std::expected<void, CommandRefusal> applyReadingLimits(EditSession &session, const std::optional<ReadingLimits> &limits);

}
