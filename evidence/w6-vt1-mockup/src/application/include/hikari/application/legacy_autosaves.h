#pragma once

// P9: the legacy app's autosaves (Options.pathfull/Subs, written by
// SubsGrid::OnBackupTimer as "<subtitle name>_<tab>_<number>.<ext>"), listed
// as legacy AutoSaveOpen::GenerateList lists them (AutoSaveOpen.cpp:123-184
// and DateCompare, AutoSaveOpen.h:27-53, at 20d647c4). Reading only: the
// folder and its files are never written, renamed or removed here, and an
// autosave opens as a new unsaved copy (approved L58-recovery-copy).

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
    // The subtitle file's name as the Files list shows it: the autosave name
    // without "_<tab>_<number>" ("<untitled>.<ext>" when nothing is left).
    std::u16string name;
    std::vector<LegacyAutosaveVersion> versions; // newest first
};

// The local time of a file's modification, broken down (legacy
// FileTimeToSystemTime + SystemTimeToTzSpecificLocalTime); the default reads
// the system's time zone.
struct LocalTime {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};
using LocalTimeOf = std::function<LocalTime(std::filesystem::file_time_type)>;
LocalTime systemLocalTime(std::filesystem::file_time_type time);

// GenerateList over `folder`: every non-empty file (FindFirstFileW("Subs/*")
// in the platform's listing order, hidden ones included; folders and empty
// files skipped, as nFileSizeLow == 0 skipped them), grouped by name in the
// order first seen. Names starting with "DummySubs" that leave nothing are
// skipped; `untitled` names the others that leave nothing (legacy
// _("Untitled")). Versions are ordered newest first by DateCompare; two
// versions written in the same second keep only the first listed (a
// std::map keyed by the time text). An unreadable or empty folder gives none.
std::vector<LegacyAutosaveFile> listLegacyAutosaves(const std::filesystem::path &folder,
                                                    const std::u16string &untitled,
                                                    const LocalTimeOf &localTime = systemLocalTime);

} // namespace hikari::application
