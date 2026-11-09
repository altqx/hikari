#include "hikari/application/extents_cache.h"

#include <absl/container/flat_hash_set.h>

namespace hikari::application::karaoke_layout {

std::string ExtentsCache::styleKey(const std::vector<std::string> &style)
{
    std::string key;
    for (const std::string &f : style)
        key += f + ',';
    return key;
}

ExtentsCache::Result ExtentsCache::measure(const std::vector<std::string> &style, const std::string &text)
{
    auto key = std::pair{styleKey(style), text};
    if (const auto at = m_cache.find(key); at != m_cache.end())
        return {at->second, false};
    ++m_measured;
    const auto e = m_measure.measure(style, text);
    m_cache.emplace(std::move(key), e);
    return {e, true};
}

std::vector<ExtentsCache::Result> ExtentsCache::measureMany(const std::vector<std::string> &style,
                                                            const std::vector<std::string> &texts)
{
    const std::string key = styleKey(style);
    std::vector<std::string> misses;
    absl::flat_hash_set<std::string> missed;
    for (const auto &text : texts)
        if (!m_cache.contains(std::pair{key, text}) && missed.insert(text).second)
            misses.push_back(text);
    if (!misses.empty()) {
        const auto measured = m_measure.measureMany(style, misses);
        m_measured += misses.size();
        for (std::size_t i = 0; i < misses.size(); ++i)
            m_cache.emplace(std::pair{key, misses[i]}, i < measured.size() ? measured[i] : std::nullopt);
    }
    std::vector<Result> out;
    out.reserve(texts.size());
    for (const auto &text : texts)
        out.push_back({m_cache.at(std::pair{key, text}), missed.erase(text) != 0});
    return out;
}

}
