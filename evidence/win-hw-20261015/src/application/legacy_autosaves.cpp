#include "hikari/application/legacy_autosaves.h"

#include "hikari/application/legacy_dir.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <tuple>

namespace hikari::application {

namespace {

// wxString::BeforeLast(ch, &rest): rest is what follows the last `ch`, or
// the whole string when there is none (then the result is empty).
std::u16string beforeLast(const std::u16string &s, char16_t c, std::u16string &rest)
{
    const auto at = s.rfind(c);
    if (at == std::u16string::npos) {
        rest = s;
        return {};
    }
    rest = s.substr(at + 1);
    return s.substr(0, at);
}

std::tuple<int, int, int, int, int, int> key(const LocalTime &t)
{
    return {t.year, t.month, t.day, t.hour, t.minute, t.second};
}

} // namespace

LocalTime systemLocalTime(std::filesystem::file_time_type time)
{
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    const std::time_t seconds = std::chrono::system_clock::to_time_t(
        std::chrono::floor<std::chrono::seconds>(system));
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec};
}

std::vector<LegacyAutosaveFile> listLegacyAutosaves(const std::filesystem::path &folder, const std::u16string &untitled,
                                                    const LocalTimeOf &localTime)
{
    std::vector<LegacyAutosaveFile> out;
    // The versions of each file as legacy's std::map<wxString, wxString,
    // DateCompare>: newest first, one per second.
    std::vector<std::vector<std::pair<LocalTime, std::filesystem::path>>> versions;
    const auto files = legacy_dir::entries(folder, u"*", legacy_dir::Files | legacy_dir::Hidden);
    if (!files)
        return out; // "Cannot open auto save folder"
    for (const auto &file : *files) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec || (size & 0xffffffffu) == 0)
            continue; // data.nFileSizeLow == 0
        const auto modified = std::filesystem::last_write_time(file, ec);
        if (ec)
            continue;
        const std::u16string fileName = file.filename().u16string();
        std::u16string ext, fileNum, tabNum;
        const std::u16string rest = beforeLast(fileName, u'.', ext);
        const std::u16string rest1 = beforeLast(rest, u'_', fileNum);
        std::u16string stripped = beforeLast(rest1, u'_', tabNum);
        if (stripped.empty()) {
            if (fileName.starts_with(u"DummySubs"))
                continue;
            stripped = untitled;
        }
        stripped += u'.';
        stripped += ext;
        const LocalTime when = localTime(modified);
        const auto found = std::ranges::find(out, stripped, &LegacyAutosaveFile::name);
        std::size_t index = static_cast<std::size_t>(found - out.begin());
        if (found == out.end()) {
            out.push_back({stripped, {}});
            versions.emplace_back();
        }
        auto &list = versions[index];
        // DateCompare: y, m, d, h, mi, s, all descending; an equal time is the same key.
        const auto at = std::ranges::find_if(list, [&](const auto &v) { return key(v.first) <= key(when); });
        if (at != list.end() && key(at->first) == key(when))
            continue; // std::map::insert keeps the first
        list.insert(at, {when, file});
    }
    for (std::size_t i = 0; i < out.size(); ++i)
        for (const auto &[when, file] : versions[i]) {
            char text[64];
            std::snprintf(text, sizeof text, "%02i/%02i/%02i   %02i:%02i:%02i", when.month, when.day, when.year,
                          when.hour, when.minute, when.second);
            out[i].versions.push_back({text, file});
        }
    return out;
}

} // namespace hikari::application
