#include "hikari/application/document_scripts.h"

#include "hikari/application/automation_services.h"

#include <algorithm>
#include <filesystem>

namespace hikari::application {

namespace {

std::string_view trimLeft(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || (s.front() >= '\t' && s.front() <= '\r')))
        s.remove_prefix(1);
    return s;
}

}

std::vector<std::string> scriptInfoScriptPaths(std::string_view value, std::string_view subtitleFolder)
{
    namespace fs = std::filesystem;
    value = trimLeft(value);
    std::vector<std::string> tokens;
    std::size_t start = 0;
    bool besideSubtitle = false;
    for (;;) {
        const std::size_t end = value.find_first_of("|~$", start);
        std::string token(trimLeft(value.substr(start, end == std::string_view::npos ? end : end - start)));
        const fs::path path(token);
        if (besideSubtitle && !subtitleFolder.empty() && !token.empty() && path.is_relative())
            token = (fs::path(subtitleFolder) / path).lexically_normal().string();
        tokens.push_back(std::move(token));
        if (end == std::string_view::npos)
            break;
        besideSubtitle = value[end] == '~';
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

std::string scriptInfoPathFor(const std::string &script, const std::string &subtitleFolder,
                              const std::string &projectRoot, bool windows)
{
    namespace fs = std::filesystem;
    if (projectRoot.empty() || subtitleFolder.empty())
        return script;
    const std::string automation = (fs::path(projectRoot) / ".hikari" / "automation").string();
    if (!scriptInFolder(script, automation, windows))
        return script;
    const fs::path relative = fs::path(script).lexically_relative(subtitleFolder);
    return relative.empty() ? script : '~' + relative.generic_string();
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
                                     std::vector<std::string> *added, std::string_view subtitleFolder)
{
    if (!reads(value))
        return false;
    for (const std::string &path : scriptInfoScriptPaths(value, subtitleFolder)) {
        if (!isFile(path))
            continue;
        if (add(path) && added)
            added->push_back(path);
    }
    m_lastValue = std::string(trimLeft(value));
    return true;
}

}
