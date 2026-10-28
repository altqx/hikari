#include "hikari/application/document_scripts.h"

#include <algorithm>

namespace hikari::application {

namespace {

// wxString::Trim(false): leading whitespace (wxSafeIsspace).
std::string_view trimLeft(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || (s.front() >= '\t' && s.front() <= '\r')))
        s.remove_prefix(1);
    return s;
}

} // namespace

std::vector<std::string> scriptInfoScriptPaths(std::string_view value)
{
    // Automation.cpp:1279-1285: Trim(false), the tokenizer, each token's Trim(false).
    value = trimLeft(value);
    std::vector<std::string> tokens;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = value.find_first_of("|~$", start);
        tokens.emplace_back(trimLeft(value.substr(start, end == std::string_view::npos ? end : end - start)));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return tokens;
}

std::string scriptInfoWithScript(std::string_view value, std::string_view path)
{
    // Automation.cpp:1164-1166.
    std::string out(value);
    out += '|';
    out += path;
    return out;
}

bool DocumentScripts::add(const std::string &path)
{
    // Automation.cpp:1157-1158: a script already in the list is not added again.
    if (std::ranges::find(m_scripts, path) != m_scripts.end())
        return false;
    m_scripts.push_back(path);
    return true;
}

void DocumentScripts::remove(const std::string &path)
{
    std::erase(m_scripts, path);
}

bool DocumentScripts::readScriptInfo(const std::string &value, const std::function<bool(const std::string &)> &isFile,
                                     std::vector<std::string> *added)
{
    // Automation.cpp:1277-1278.
    if (value.empty())
        return false;
    if (value == m_lastValue && !m_scripts.empty())
        return false;
    for (const std::string &path : scriptInfoScriptPaths(value)) {
        if (!isFile(path)) // Automation.cpp:1286: a missing file is skipped silently
            continue;
        if (add(path) && added)
            added->push_back(path);
    }
    // Automation.cpp:1305, after paths.Trim(false).
    m_lastValue = std::string(trimLeft(value));
    return true;
}

} // namespace hikari::application
