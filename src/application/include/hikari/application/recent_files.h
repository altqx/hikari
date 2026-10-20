#pragma once

// P2: the recent subtitles list and how opened files are dispatched.
//
// RecentFiles follows legacy HikariSubFrame::SetRecent/AppendRecent: most
// recent first, no duplicates, at most 20. Missing local files are pruned
// when the list is shown; network paths are not checked, so a missing share
// never stalls the menu.
//
// SH2: an entry can also carry when it was last opened, for the Home page.
// Legacy kept paths only, so the times are stored beside the list
// (openedRecords) and an entry carried over from legacy has none.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

class RecentFiles {
public:
    static constexpr std::size_t kCapacity = 20;

    // `path` first; `openedMs` (milliseconds since the epoch) is when it was
    // opened, none forgets any earlier time.
    void add(const std::string &path, std::optional<std::int64_t> openedMs = std::nullopt);
    // Replaces the list (from storage), keeping its order and the rules. The
    // times are forgotten; setOpenedRecords gives them back.
    void set(std::vector<std::string> paths);
    const std::vector<std::string> &entries() const { return m_paths; }
    // Drops entries `missing` reports; true when anything was dropped.
    bool prune(const std::function<bool(const std::string &)> &missing);
    // Drops `path`; true when it was listed.
    bool remove(const std::string &path);
    std::optional<std::int64_t> opened(const std::string &path) const;
    // The times as stored: "<ms> <path>" for each entry that has one, in the
    // list's order. Reading takes the records of listed paths and skips the
    // rest.
    std::vector<std::string> openedRecords() const;
    void setOpenedRecords(const std::vector<std::string> &records);

private:
    void forgetUnlisted();

    std::vector<std::string> m_paths;
    std::map<std::string, std::int64_t> m_opened;
};

// Legacy IsMissingLocalFile: UNC paths and mapped network drives (as
// `isRemoteDrive` reports them) count as present without a check.
bool isMissingLocalFile(const std::string &path, const std::function<bool(const std::string &)> &exists,
                        const std::function<bool(char drive)> &isRemoteDrive);

// What opening a file does, by its extension (legacy OpenFile for one file,
// OpenFiles for several): subtitles replace the editing target's content,
// scripts load into Automation, keyframes go to the video, archives and
// programs are refused, anything else opens as video.
enum class OpenKind { Subtitles, Script, Keyframes, Video, Refused };
OpenKind openKindOf(std::string_view path, bool single);

} // namespace hikari::application
