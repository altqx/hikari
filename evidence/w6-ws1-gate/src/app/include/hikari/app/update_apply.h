#pragma once

// The Windows installing side of an update (ADR 0019):
//
//   HikariSub.exe --hikari-apply-update <version> <file> <pid> <0|1 relaunch>
//
// The app never hands the downloaded installer to UAC. For an all-users
// install it elevates the installed HikariSub.exe in Program Files, which
// only administrators can change, with this switch; for a per-user install
// it runs the same unelevated. It elevates only a program the user cannot
// change (isProtectedFromUser): the scope comes from package-kind.txt, which
// is no more protected than the folder it is in, so a per-user copy that
// claims to be for all users is never elevated, nor one installed for all
// users into a folder the user can write. That process trusts nothing the user's side
// wrote (the pending record, its SHA-256, the download folder):
//   1. <version> must be a release version newer than its own;
//   2. it downloads the release's SHA256SUMS itself, over HTTPS from the
//      fixed GitHub URL, and takes the digest listed for
//      HikariSub-<version>-windows-x64-setup.exe;
//   3. elevated, it copies <file> into a new folder under %windir%\Temp that
//      only SYSTEM and administrators can write; per-user, it takes <file>
//      where it is. It opens that file denying writes and deletes, hashes it
//      through that handle, and only when the hash is the listed one starts
//      it in /UPDATE mode for its own install folder, keeping the handle
//      open until the installer has started.
// It exits with application::kApply*: 0 once the installer runs, 2 for a bad
// request, 3 when the file is not the release's, 4 when it cannot copy or
// start it, 6 when SHA256SUMS cannot be downloaded.

#ifdef _WIN32

#include "hikari/application/update_check.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::app::update_apply {

struct Environment {
    // This copy's version and install folder, and its scope.
    std::string installed;
    std::wstring installDir;
    bool allUsers = false;
    // Where the private copy's folder is made, for all users.
    std::wstring stagingParent;
    // The release's SHA256SUMS, or nothing when it cannot be had.
    std::function<std::optional<std::string>(const std::string &version)> fetchSums;
    // Starts the installer with its arguments; false when it cannot.
    std::function<bool(const std::wstring &installer, const std::wstring &arguments)> run;
};

int apply(const application::ApplyRequest &request, const Environment &environment);

// The switch and its arguments as one command line tail, quoted as
// CommandLineToArgvW reads it.
std::wstring commandLine(const application::ApplyRequest &request);

// The real pieces of an Environment.
// A GET of at most `limit` bytes over WinHTTP. Every URL on the way,
// redirects included, must pass `allowed`, and every server's certificate
// must chain to a root in the local machine's stores, not one the current
// user added.
std::optional<std::string> fetchHttps(const std::string &url,
                                      const std::function<bool(const std::string &finalUrl)> &allowed,
                                      std::size_t limit);
// CreateProcessW, with the installer's folder as its working folder. It
// returns once the installer holds kSetupMutex (or has exited, or after
// 10 s): an app started after this one's quit sees it.
bool launch(const std::wstring &installer, const std::wstring &arguments);
std::wstring systemTempFolder(); // %windir%\Temp

// Held by an update's setup (/UPDATE) from its start to its exit, while it
// waits for the app and replaces its files.
inline constexpr wchar_t kSetupMutex[] = L"Global\\io.github.altqx.HikariSub.Installing";
// Whether an update's setup runs now: kSetupMutex exists, even one this
// process may not open (an elevated setup's).
bool setupRunning();
// Made by the same setup, before kSetupMutex, for any signed-in user to set:
// an app started meanwhile asks for a start after the update. An event, not
// a file: an all-users setup that another administrator approved runs in
// that account's profile, not the user's.
inline constexpr wchar_t kStartAfterSetup[] = L"Global\\io.github.altqx.HikariSub.StartAfterUpdate";
// A start while an update's setup runs would hold the program open and stop
// the update: it leaves at once and sets kStartAfterSetup, and the setup
// starts HikariSub as the desktop user when it is done. False when the
// event cannot be set.
bool askStartAfterSetup();
bool isElevated();

// What the user may do to a path: the access mask AccessCheck grants for
// MAXIMUM_ALLOWED, or nothing when it cannot be told.
using RightsOf = std::function<std::optional<unsigned long>(const std::wstring &path)>;
struct Entry {
    std::wstring name;
    bool folder = false;
    bool reparse = false; // a symbolic link, junction or other reparse point
};
// A folder's entries, or nothing when it cannot be listed.
using ListOf = std::function<std::optional<std::vector<Entry>>(const std::wstring &folder)>;
// Whether elevating the program in installDir, a plain "X:\..." path, runs
// only what administrators put there: the user can write, delete, re-own or
// re-permission nothing in installDir or under it, and can neither delete
// nor replace any folder on the way to it. Anything that cannot be told, a
// reparse point in the folder among them, counts as changeable.
bool isProtected(const std::wstring &installDir, const RightsOf &rightsOf, const ListOf &list);
// isProtected() for the user of this process, on the disk.
bool isProtectedFromUser(const std::wstring &installDir);

// The switch's whole run, from the process's own command line: this copy's
// version, its folder, its scope (all users when elevated from an
// nsis-machine install), the real fetch and launch. Returns the exit code.
int runFromCommandLine(const std::string &installedVersion, bool allUsersInstall, const std::string &testFeed);

} // namespace hikari::app::update_apply

#endif
