#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::explorer {

enum class FileKind { Subtitles, Video, Audio, Keyframes, Timecodes, Fonts, Chapters, Other };

// By name alone: extensions match case-insensitively, and a .txt is keyframes, timecodes or chapters
// when its name says so.
FileKind classify(std::string_view name);
bool opensAsMedia(FileKind kind);

// `*` and `?` stay inside one path part, `**` crosses them, `[...]` is a set. A pattern without
// `/` matches the name, one with `/` the path from the Project folder.
bool globMatches(std::string_view pattern, std::string_view text);

// Case-insensitive for ASCII letters, digit runs by value ("ep2" before "ep10"), otherwise by code point.
bool naturalLess(std::string_view a, std::string_view b);

struct Filter {
    std::vector<std::string> excludes;
    bool allFiles = false;
};

// relative: from the Project folder, with `/` separators.
bool excluded(const Filter &filter, std::string_view relative, std::string_view name);
bool fileShown(const Filter &filter, std::string_view relative, std::string_view name, bool hidden);
bool folderShown(const Filter &filter, std::string_view relative, std::string_view name, bool hidden);

struct Group {
    std::size_t lead;
    std::vector<std::size_t> members;
};

// Group by name, as findSameNamedFile matches "<stem>.*": each video's or subtitle file's stem
// gathers the files named "<stem>.*", and keyframes, timecodes and chapters named "<stem>_*" too. A
// file joins the shortest stem that takes it. The lead is the first subtitle file, else the video.
// `names` are one folder's files, sorted; groups of one file are left out.
std::vector<Group> groupByName(const std::vector<std::string> &names);

inline constexpr std::string_view kDefaultExcludes[] = {"*.lwi", "*.ffindex"};

}
