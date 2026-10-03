#pragma once

// The Grid's CPS and Wraps measures (G7; legacy SpellChecker::
// CheckTextAndBrackets, SpellChecker::Check and TextData::GetCPS at
// 20d647c4). Override blocks ({...}, <...> in SRT) and drawings are skipped;
// \N (| outside ASS and SRT) ends a wrap. Characters are the lengths of the
// word segments holding letters or numbers (legacy counts boost::locale word
// segments of those kinds), unless spaces and punctuation are counted too.

#include "hikari/core/document.h"

#include <QString>

#include <cstdint>

namespace hikari::ui {

struct LineMeasures {
    int chars = 0;
    QString wraps;          // "12/30", one count per wrap ("0" for empty text)
    bool badWraps = false;  // a wrap over 43 characters, or three wraps or more
};

// CALC_SPACES_AND_PUNCTATION_FOR_CPS / _FOR_WRAPS (off by default).
struct MeasureOptions {
    bool allCharsForCps = false;
    bool allCharsForWraps = false;
};

LineMeasures measureLine(const QString &text, core::SubtitleFormat format, MeasureOptions options = {});
// TextData::GetCPS: characters per second, truncated; 999 when negative,
// over 999 or undefined (no duration).
int legacyCps(int chars, std::int64_t startMs, std::int64_t endMs);

} // namespace hikari::ui
