#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

inline constexpr std::string_view kProjectFolder = ".hikari";

// The nearest folder holding `.hikari/`, starting at `file`'s own folder and going up.
std::optional<std::filesystem::path> nearestProjectRoot(const std::filesystem::path &file);

// The folder's own name; a drive or filesystem root gives the whole path.
std::string projectName(const std::filesystem::path &root);

// A trusted folder covers its subfolders.
bool folderTrusted(const std::string &root, const std::vector<std::string> &trustedFolders, bool windows);

// A file name for what HikariSub keeps about the Project in the user folder.
std::string projectKey(const std::string &canonicalRoot);

// -n / --new-window and -r / --reuse-window.
enum class WindowRequest { Default, New, Reuse };

enum class FolderPlace { Focus, Current, NewWindow };

// A Project already open in a window brings that window to the front. Otherwise -r, or a current
// window with no Project and no Documents, takes it; anything else opens a new window.
FolderPlace placeFolder(bool openInAWindow, bool currentEmpty, WindowRequest request);

}
