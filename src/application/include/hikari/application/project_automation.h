#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application {

struct WindowProject {
    std::string root; // empty: the window has no Project
    bool trusted = false;
};

// A folder and the file names listed in it.
using FolderListing = std::pair<std::string, std::vector<std::string>>;

std::string projectAutoloadFolder(const std::string &root, bool windows);
std::string projectIncludeFolder(const std::string &root, bool windows);

struct WindowScripts {
    int window = 0;
    WindowProject project;
    std::vector<std::string> projectNames; // the file names in the Project's autoload folder
};

struct AutoloadPlan {
    std::vector<std::string> load; // what every window shows, each script once
    // By window: shipped < user < its trusted Project, a later script hiding an earlier one of the same name.
    std::map<int, std::vector<std::string>> shown;
    // By window: its untrusted Project's scripts, which don't load.
    std::map<int, std::vector<std::string>> locked;
};

// `base`: the shipped autoload listing, then the user's.
AutoloadPlan planAutoload(const std::vector<FolderListing> &base, const std::vector<WindowScripts> &windows,
                          bool windowsPaths);

// The include path: a script inside a trusted Project's folder gets that Project's include folder before
// `base`, any other script `base` only. The Lua helper still loads a shipped module from the shipped folder.
std::vector<std::string> includeFoldersFor(const std::string &script, const std::vector<std::string> &base,
                                           const std::vector<std::string> &trustedRoots, bool windows);

}
