#pragma once

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace hikari::ui {

struct KaraokeReport {
    struct Template {
        int line = 0;
        QString effect;
        QString style;
        double seconds = 0;
        int generated = 0;
    };
    bool selected = false;
    double seconds = 0, prepareSeconds = 0;
    int karaokeLines = 0, generated = 0, problems = 0;
    std::vector<Template> templates;
};

std::optional<KaraokeReport> readKaraokeReport(const std::string &report);

QString karaokeReportSummary(const KaraokeReport &report);

QString karaokeReportDetails(const KaraokeReport &report);

}
