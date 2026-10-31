#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hikari::application {

inline constexpr char kAutomationScriptsKey[] = "Automation Scripts";

// Splits at every '|', '~' and '$', keeping empty tokens; each token loses
// its leading whitespace.
std::vector<std::string> scriptInfoScriptPaths(std::string_view value);

// value + "|" + path, so a first script gives "|<path>".
std::string scriptInfoWithScript(std::string_view value, std::string_view path);

class DocumentScripts {
public:
    // In the order they were added.
    const std::vector<std::string> &scripts() const { return m_scripts; }
    bool empty() const { return m_scripts.empty(); }
    // False when the same path text is listed already.
    bool add(const std::string &path);
    void remove(const std::string &path);
    // False for an empty value, or for the value read last while scripts are listed.
    bool reads(const std::string &value) const { return !value.empty() && (value != m_lastValue || m_scripts.empty()); }
    // False (nothing read) when it doesn't read the value; otherwise adds every unlisted token naming an
    // existing file, `added` receiving the new ones.
    bool readScriptInfo(const std::string &value, const std::function<bool(const std::string &)> &isFile,
                        std::vector<std::string> *added = nullptr);

private:
    std::vector<std::string> m_scripts;
    std::unordered_set<std::string> m_listed;
    std::string m_lastValue;
};

}
