#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/problems.h"
#include "hikari/application/reading_limits.h"
#include "hikari/core/document.h"
#include "line_measures.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::app {

// The Lines that break a reading limit, as problems; Comment Lines are skipped.
std::vector<application::Problem> readingProblems(const core::Document &document, const application::ReadingLimits &limits,
                                                  ui::MeasureOptions measure);

// The built-in "reading" source: keeps every open Document's reading problems current.
class ReadingProblems {
public:
    static constexpr std::string_view kSource = "reading";

    explicit ReadingProblems(application::Problems &problems) : m_problems(problems) {}

    struct Settings {
        bool shown = true;
        application::ReadingLimits profile;
        bool allCharsForCps = false, allCharsForWraps = false;
        friend bool operator==(const Settings &, const Settings &) = default;
    };
    void setSettings(const Settings &settings);
    // Sets again each Document whose revision moved; forgets the closed ones.
    void sync(const std::vector<std::pair<application::DocumentId, const application::EditSession *>> &open);

private:
    struct Seen {
        std::optional<std::uint64_t> revision;
        std::vector<application::Problem> problems;
    };
    application::Problems &m_problems;
    Settings m_settings;
    std::map<std::uint64_t, Seen> m_seen;
};

}
