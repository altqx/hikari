#pragma once

#include "hikari/core/document.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <absl/container/flat_hash_set.h>
#include <vector>

namespace hikari::application {

inline constexpr char kAutomationScriptsKey[] = "Automation Scripts";

// Project Garbage first, then Script Info (older files).
std::optional<std::string> documentScriptsValue(const core::Document &document);
// Into Project Garbage, and into Script Info only where the file already has it there.
bool setDocumentScriptsValue(core::Document &document, const std::string &value);

// Splits at every '|', '~' and '$', keeping empty tokens; each token loses
// its leading whitespace. A relative path after '~' joins subtitleFolder, as Aegisub reads it.
std::vector<std::string> scriptInfoScriptPaths(std::string_view value, std::string_view subtitleFolder = {});

// value + "|" + path, so a first script gives "|<path>".
std::string scriptInfoWithScript(std::string_view value, std::string_view path);

// "~" and the path from subtitleFolder for a script under the Project's .hikari/automation/, so Aegisub
// finds it too; otherwise the path itself.
std::string scriptInfoPathFor(const std::string &script, const std::string &subtitleFolder,
                              const std::string &projectRoot, bool windows);

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
                        std::vector<std::string> *added = nullptr, std::string_view subtitleFolder = {});

private:
    std::vector<std::string> m_scripts;
    absl::flat_hash_set<std::string> m_listed;
    std::string m_lastValue;
};

}
