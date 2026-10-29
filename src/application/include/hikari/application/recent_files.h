#pragma once

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

    // `openedMs`: milliseconds since the epoch; none forgets any earlier time.
    void add(const std::string &path, std::optional<std::int64_t> openedMs = std::nullopt);
    // Forgets the times; setOpenedRecords gives them back.
    void set(std::vector<std::string> paths);
    const std::vector<std::string> &entries() const { return m_paths; }
    // True when anything was dropped.
    bool prune(const std::function<bool(const std::string &)> &missing);
    // True when `path` was listed.
    bool remove(const std::string &path);
    std::optional<std::int64_t> opened(const std::string &path) const;
    // "<ms> <path>" per entry with a time, in list order; setOpenedRecords
    // skips unlisted paths.
    std::vector<std::string> openedRecords() const;
    void setOpenedRecords(const std::vector<std::string> &records);

private:
    void forgetUnlisted();

    std::vector<std::string> m_paths;
    std::map<std::string, std::int64_t> m_opened;
};

// UNC paths and `isRemoteDrive` drives count as present without a check.
bool isMissingLocalFile(const std::string &path, const std::function<bool(const std::string &)> &exists,
                        const std::function<bool(char drive)> &isRemoteDrive);

// By extension: unknown ones open as video, archives and programs are
// refused, a session file (.hksession, .kls) only opens alone.
enum class OpenKind { Subtitles, Script, Keyframes, Video, Session, Refused };
OpenKind openKindOf(std::string_view path, bool single);

}
