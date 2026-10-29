#pragma once

// Read only: HikariSub legacy's autosave folder and files are never written, renamed or removed.

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace hikari::application {

struct LegacyAutosaveVersion {
    std::string written; // "MM/DD/YYYY   hh:mm:ss", the file's local modification time
    std::filesystem::path file;
};

struct LegacyAutosaveFile {
    // The autosave name without "_<tab>_<number>" ("<untitled>.<ext>" when nothing is left).
    std::u16string name;
    std::vector<LegacyAutosaveVersion> versions; // newest first
};

struct LocalTime {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};
using LocalTimeOf = std::function<LocalTime(std::filesystem::file_time_type)>;
LocalTime systemLocalTime(std::filesystem::file_time_type time);

// Every non-empty file, grouped by name in first-seen order. "DummySubs" names that leave
// nothing are skipped; `untitled` names the others that leave nothing. Versions go newest first,
// one per second (the first listed wins). An unreadable or empty folder gives none.
std::vector<LegacyAutosaveFile> listLegacyAutosaves(const std::filesystem::path &folder,
                                                    const std::u16string &untitled,
                                                    const LocalTimeOf &localTime = systemLocalTime);

} // namespace hikari::application
