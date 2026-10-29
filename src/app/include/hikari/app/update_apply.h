#pragma once

//   HikariSub.exe --hikari-apply-update <version> <file> <pid> <0|1 relaunch>
// It trusts nothing the user's side wrote: it fetches SHA256SUMS itself and hashes a locked copy.
// Exit codes (application::kApply*): 0 installer runs, 2 bad request, 3 not the release's file,
// 4 cannot copy or start it, 6 SHA256SUMS cannot be downloaded.

#ifdef _WIN32

#include "hikari/application/update_check.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::app::update_apply {

struct Environment {
    std::string installed;
    std::wstring installDir;
    bool allUsers = false;
    // Parent of the private copy's folder (all-users installs only).
    std::wstring stagingParent;
    // nullopt when SHA256SUMS cannot be fetched.
    std::function<std::optional<std::string>(const std::string &version)> fetchSums;
    std::function<bool(const std::wstring &installer, const std::wstring &arguments)> run;
};

int apply(const application::ApplyRequest &request, const Environment &environment);

// Quoted as CommandLineToArgvW reads it.
std::wstring commandLine(const application::ApplyRequest &request);

// Every URL, redirects included, must pass `allowed`; certificates must chain to a root in the
// local machine's stores, not one the current user added.
std::optional<std::string> fetchHttps(const std::string &url,
                                      const std::function<bool(const std::string &finalUrl)> &allowed,
                                      std::size_t limit);
// Returns once the installer holds kSetupMutex, has exited, or after 10 s.
bool launch(const std::wstring &installer, const std::wstring &arguments);
std::wstring systemTempFolder(); // %windir%\Temp

// Held by an update's setup (/UPDATE) from its start to its exit.
inline constexpr wchar_t kSetupMutex[] = L"Global\\io.github.altqx.HikariSub.Installing";
// True also when kSetupMutex exists but this process may not open it.
bool setupRunning();
// Created by the setup before kSetupMutex; any signed-in user may set it.
inline constexpr wchar_t kStartAfterSetup[] = L"Global\\io.github.altqx.HikariSub.StartAfterUpdate";
// Sets kStartAfterSetup so the setup starts HikariSub when done; false when it cannot be set.
bool askStartAfterSetup();
bool isElevated();

// The access mask AccessCheck grants for MAXIMUM_ALLOWED; nullopt when it cannot be told.
using RightsOf = std::function<std::optional<unsigned long>(const std::wstring &path)>;
struct Entry {
    std::wstring name;
    bool folder = false;
    bool reparse = false; // a symbolic link, junction or other reparse point
};
// nullopt when the folder cannot be listed.
using ListOf = std::function<std::optional<std::vector<Entry>>(const std::wstring &folder)>;
// True only when the user can change nothing in or under installDir (a plain "X:\..." path) and
// can replace no folder on the way to it. Anything unknown, reparse points included, is changeable.
bool isProtected(const std::wstring &installDir, const RightsOf &rightsOf, const ListOf &list);
bool isProtectedFromUser(const std::wstring &installDir);

// Returns the exit code.
int runFromCommandLine(const std::string &installedVersion, bool allUsersInstall, const std::string &testFeed);

} // namespace hikari::app::update_apply

#endif
