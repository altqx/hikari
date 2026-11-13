#include "hikari/application/explorer.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>

namespace hikari::application::explorer {

namespace {

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowered(std::string_view s)
{
    std::string out(s);
    std::ranges::transform(out, out.begin(), lower);
    return out;
}

std::string_view extension(std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos || dot == 0 ? std::string_view() : name.substr(dot + 1);
}

std::string_view stem(std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

template <std::size_t N> bool among(const std::array<std::string_view, N> &list, std::string_view value)
{
    return std::ranges::find(list, value) != list.end();
}

constexpr std::array<std::string_view, 6> kSubtitles{"ass", "ssa", "srt", "sub", "txt", "mpl"};
constexpr std::array<std::string_view, 15> kVideos{"avi", "mp4", "mkv",  "ogm", "wmv", "asf", "rmvb", "rm",
                                                   "3gp", "ts",  "m2ts", "m4v", "flv", "webm", "mov"};
constexpr std::array<std::string_view, 12> kAudio{"wav", "w64", "flac", "ac3", "eac3", "aac",
                                                  "ogg", "opus", "mp3", "m4a", "mka", "dts"};
constexpr std::array<std::string_view, 4> kFonts{"ttf", "otf", "ttc", "pfb"};

bool characterMatches(std::string_view set, char c)
{
    const bool negated = !set.empty() && (set.front() == '!' || set.front() == '^');
    if (negated)
        set.remove_prefix(1);
    bool found = false;
    for (std::size_t i = 0; i < set.size(); ++i) {
        if (i + 2 < set.size() && set[i + 1] == '-') {
            found = found || (lower(c) >= lower(set[i]) && lower(c) <= lower(set[i + 2]));
            i += 2;
        } else {
            found = found || lower(c) == lower(set[i]);
        }
    }
    return found != negated;
}

bool matchFrom(std::string_view pattern, std::string_view text)
{
    while (!pattern.empty()) {
        const char p = pattern.front();
        if (p == '*') {
            const bool crosses = pattern.starts_with("**");
            pattern.remove_prefix(crosses ? 2 : 1);
            if (crosses && pattern.starts_with('/') && matchFrom(pattern.substr(1), text))
                return true;
            for (std::size_t i = 0; i <= text.size(); ++i) {
                if (matchFrom(pattern, text.substr(i)))
                    return true;
                if (i < text.size() && text[i] == '/' && !crosses)
                    return false;
            }
            return false;
        }
        if (text.empty())
            return false;
        if (p == '?') {
            if (text.front() == '/')
                return false;
        } else if (p == '[') {
            const auto close = pattern.find(']', 2);
            if (close == std::string_view::npos) {
                if (text.front() != '[')
                    return false;
            } else {
                if (text.front() == '/' || !characterMatches(pattern.substr(1, close - 1), text.front()))
                    return false;
                pattern.remove_prefix(close);
            }
        } else if (lower(p) != lower(text.front())) {
            return false;
        }
        pattern.remove_prefix(1);
        text.remove_prefix(1);
    }
    return text.empty();
}

bool hiddenName(std::string_view name, bool hidden)
{
    return hidden || name.starts_with('.');
}

bool joinsByUnderscore(FileKind kind)
{
    return kind == FileKind::Keyframes || kind == FileKind::Timecodes || kind == FileKind::Chapters;
}

}

FileKind classify(std::string_view name)
{
    const std::string ext = lowered(extension(name));
    const std::string base = lowered(stem(name));
    const auto says = [&base](std::string_view word) { return base.find(word) != std::string::npos; };
    if (ext == "pass" || ext == "stats" || ((ext == "txt" || ext == "log") && says("keyframe")))
        return FileKind::Keyframes;
    if (ext == "txt" && (says("timecode") || base.ends_with("_tc") || base.ends_with(".tc")))
        return FileKind::Timecodes;
    if ((ext == "xml" || ext == "txt") && says("chapter"))
        return FileKind::Chapters;
    if (among(kSubtitles, ext))
        return FileKind::Subtitles;
    if (among(kVideos, ext))
        return FileKind::Video;
    if (among(kAudio, ext))
        return FileKind::Audio;
    if (among(kFonts, ext))
        return FileKind::Fonts;
    return FileKind::Other;
}

bool naturalLess(std::string_view a, std::string_view b)
{
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            const std::size_t aStart = i;
            const std::size_t bStart = j;
            while (i < a.size() && digit(a[i]))
                ++i;
            while (j < b.size() && digit(b[j]))
                ++j;
            std::string_view x = a.substr(aStart, i - aStart);
            std::string_view y = b.substr(bStart, j - bStart);
            while (x.size() > 1 && x.front() == '0')
                x.remove_prefix(1);
            while (y.size() > 1 && y.front() == '0')
                y.remove_prefix(1);
            if (x.size() != y.size())
                return x.size() < y.size();
            if (x != y)
                return x < y;
            continue;
        }
        const auto x = static_cast<unsigned char>(lower(a[i]));
        const auto y = static_cast<unsigned char>(lower(b[j]));
        if (x != y)
            return x < y;
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j)
        return a.size() - i < b.size() - j;
    return a < b;
}

bool opensAsMedia(FileKind kind)
{
    return kind == FileKind::Video || kind == FileKind::Audio || kind == FileKind::Keyframes ||
           kind == FileKind::Timecodes;
}

bool globMatches(std::string_view pattern, std::string_view text)
{
    if (pattern.starts_with('/'))
        pattern.remove_prefix(1);
    return !pattern.empty() && matchFrom(pattern, text);
}

bool excluded(const Filter &filter, std::string_view relative, std::string_view name)
{
    return std::ranges::any_of(filter.excludes, [&](const std::string &pattern) {
        return globMatches(pattern, pattern.find('/') == std::string::npos ? name : relative);
    });
}

bool fileShown(const Filter &filter, std::string_view relative, std::string_view name, bool hidden)
{
    if (hiddenName(name, hidden) || excluded(filter, relative, name))
        return false;
    return filter.allFiles || classify(name) != FileKind::Other;
}

bool folderShown(const Filter &filter, std::string_view relative, std::string_view name, bool hidden)
{
    return !hiddenName(name, hidden) && !excluded(filter, relative, name);
}

std::vector<Group> groupByName(const std::vector<std::string> &names)
{
    std::vector<FileKind> kinds;
    kinds.reserve(names.size());
    std::vector<std::string_view> stems;
    for (const auto &name : names) {
        kinds.push_back(classify(name));
        if (kinds.back() == FileKind::Video || kinds.back() == FileKind::Subtitles)
            stems.push_back(stem(name));
    }
    std::ranges::sort(stems, [](std::string_view a, std::string_view b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    std::map<std::string_view, std::vector<std::size_t>> byStem;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string_view name = names[i];
        for (const std::string_view s : stems) {
            if (name.size() <= s.size() + 1 || !name.starts_with(s))
                continue;
            const char after = name[s.size()];
            if (after == '.' || (after == '_' && joinsByUnderscore(kinds[i]))) {
                byStem[s].push_back(i);
                break;
            }
        }
    }
    std::vector<Group> groups;
    for (auto &[s, files] : byStem) {
        if (files.size() < 2)
            continue;
        const auto first = [&](FileKind kind) -> std::optional<std::size_t> {
            const auto it = std::ranges::find_if(files, [&](std::size_t i) { return kinds[i] == kind; });
            return it == files.end() ? std::nullopt : std::optional(*it);
        };
        const auto lead = first(FileKind::Subtitles).or_else([&] { return first(FileKind::Video); });
        if (!lead)
            continue;
        Group group{*lead, {}};
        for (const std::size_t i : files)
            if (i != *lead)
                group.members.push_back(i);
        groups.push_back(std::move(group));
    }
    std::ranges::sort(groups, {}, &Group::lead);
    return groups;
}

}
