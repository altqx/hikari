#pragma once

#include "hikari/core/document.h"

#include <QString>

#include <cstdint>

namespace hikari::ui {

struct LineMeasures {
    int chars = 0;
    // MUST KILL: wraps is a slash-joined count string ("12/30", "0" for empty text)
    QString wraps;
    bool badWraps = false;
};

struct MeasureOptions {
    bool allCharsForCps = false;
    bool allCharsForWraps = false;
};

LineMeasures measureLine(const QString &text, core::SubtitleFormat format, MeasureOptions options = {});
// MUST KILL: legacyCps returns 999 for a negative, over-999 or undefined CPS
int legacyCps(int chars, std::int64_t startMs, std::int64_t endMs);

}
