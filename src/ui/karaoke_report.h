#pragma once

// KE1-template-timing: the run report the bundled karaoke templater attaches
// to its result (aegisub.hikari.report; docs/karaoke-templater.md), read
// and put in words: a summary for the status bar and, per template Line, what
// it cost for the log window.

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace hikari::ui {

struct KaraokeReport {
    struct Template {
        int line = 0;    // its number in the Grid after the run
        QString effect;  // "template syl noblank", "code once", ...
        QString style;
        double seconds = 0;
        int generated = 0;
    };
    bool selected = false; // "Apply karaoke template to selected lines"
    double seconds = 0, prepareSeconds = 0;
    int karaokeLines = 0, generated = 0, problems = 0;
    std::vector<Template> templates; // in Line order
};

// Nothing for any other text, or a report of another version.
std::optional<KaraokeReport> readKaraokeReport(const std::string &report);

// One line for the status bar.
QString karaokeReportSummary(const KaraokeReport &report);

// The run and each template Line, the slowest first, one per line.
QString karaokeReportDetails(const KaraokeReport &report);

} // namespace hikari::ui
