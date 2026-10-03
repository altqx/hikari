#pragma once

// P2: the recent subtitles list and how opened files are dispatched.
//
// RecentFiles follows legacy HikariSubFrame::SetRecent/AppendRecent: most
// recent first, no duplicates, at most 20. Missing local files are pruned
// when the list is shown; network paths are not checked, so a missing share
// never stalls the menu.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

class RecentFiles {
public:
    static constexpr std::size_t kCapacity = 20;

    void add(const std::string &path);
    // Replaces the list (from storage), keeping its order and the rules.
    void set(std::vector<std::string> paths);
    const std::vector<std::string> &entries() const { return m_paths; }
    // Drops entries `missing` reports; true when anything was dropped.
    bool prune(const std::function<bool(const std::string &)> &missing);

private:
    std::vector<std::string> m_paths;
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
