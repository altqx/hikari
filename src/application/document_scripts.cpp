#include "hikari/application/document_scripts.h"

#include <algorithm>

namespace hikari::application {

namespace {

std::string_view trimLeft(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || (s.front() >= '\t' && s.front() <= '\r')))
        s.remove_prefix(1);
    return s;
}

}

std::vector<std::string> scriptInfoScriptPaths(std::string_view value)
{
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
    std::string out(value);
    out += '|';
    out += path;
    return out;
}

bool DocumentScripts::add(const std::string &path)
{
    if (!m_listed.insert(path).second)
        return false;
    m_scripts.push_back(path);
    return true;
}

void DocumentScripts::remove(const std::string &path)
{
    if (m_listed.erase(path))
        std::erase(m_scripts, path);
}

bool DocumentScripts::readScriptInfo(const std::string &value, const std::function<bool(const std::string &)> &isFile,
                                     std::vector<std::string> *added)
{
    if (value.empty())
        return false;
    if (value == m_lastValue && !m_scripts.empty())
        return false;
    for (const std::string &path : scriptInfoScriptPaths(value)) {
        if (!isFile(path))
            continue;
        if (add(path) && added)
            added->push_back(path);
    }
    m_lastValue = std::string(trimLeft(value));
    return true;
}

}
