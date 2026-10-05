#pragma once

// S4: the scripts a Document names in its Script Info ("Automation Scripts",
// legacy Automation::AddFromSubs and Add at 20d647c4, Automation.cpp:1153-1171
// and 1273-1307). Legacy keeps them in one list for the application
// (Automation::ASSScripts), apart from the autoload scripts; scripts loaded
// with Load script join it too, and "Run the last loaded script" runs the
// first macro of its last entry.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

inline constexpr char kAutomationScriptsKey[] = "Automation Scripts";

// The paths legacy reads from the value: wxStringTokenizer(paths, "|~$",
// wxTOKEN_RET_EMPTY_ALL) splits at every '|', '~' and '$' (each character
// is a delimiter, so a path holding one is cut there) and keeps empty
// tokens; the value and each token lose their leading whitespace (Trim(false)).
std::vector<std::string> scriptInfoScriptPaths(std::string_view value);

// The value after Load script adds `path` (Automation::Add with addToSinfo:
// value + "|" + path, so a first script gives "|<path>").
std::string scriptInfoWithScript(std::string_view value, std::string_view path);

class DocumentScripts {
public:
    // In the order they were added.
    const std::vector<std::string> &scripts() const { return m_scripts; }
    bool empty() const { return m_scripts.empty(); }
    // Automation::Add's check: false when the path is listed already (the
    // same text; legacy compares the file names as given).
    bool add(const std::string &path);
    // Automation::Remove.
    void remove(const std::string &path);
    // Automation::AddFromSubs: false (nothing read) for an empty value, or for
    // the value read last while scripts are listed; otherwise every token
    // naming an existing file (`isFile`, relative to the working directory as
    // wxFileExists) is added unless listed, `added` receiving the new ones.
    bool readScriptInfo(const std::string &value, const std::function<bool(const std::string &)> &isFile,
                        std::vector<std::string> *added = nullptr);

private:
    std::vector<std::string> m_scripts;
    std::string m_lastValue; // legacy Automation::scriptpaths
};

} // namespace hikari::application
