#pragma once

#include "hikari/application/automation_services.h"

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application::karaoke_layout {

// One measuring cache per run, keyed by the Style's fields and the text. A failure is kept too.
class ExtentsCache {
public:
    struct Result {
        std::optional<TextExtents> extents;
        bool measuredNow = false;
    };

    explicit ExtentsCache(TextMeasurePort &measure) : m_measure(measure) {}
    Result measure(const std::vector<std::string> &style, const std::string &text);
    // measure() of each text in order; the texts not yet cached reach the port in one measureMany.
    std::vector<Result> measureMany(const std::vector<std::string> &style, const std::vector<std::string> &texts);
    // How many texts the port has measured.
    std::size_t measured() const { return m_measured; }

private:
    static std::string styleKey(const std::vector<std::string> &style);

    TextMeasurePort &m_measure;
    absl::flat_hash_map<std::pair<std::string, std::string>, std::optional<TextExtents>> m_cache;
    std::size_t m_measured = 0;
};

}
