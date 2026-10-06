#pragma once

// Placing a font fixture file in a folder the tests do not own (the Windows
// user font folder, Y8W) and removing it again. A file that is already there
// with the fixture's bytes is the tests' own, left by a run that never got
// to its cleanup; it is reused and removed like a fresh copy. A file of that
// name with other bytes is never touched.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

namespace hikari::testing {

enum class FixturePlacement {
    Copied,  // the fixture was copied in: the tests own it
    Reused,  // the fixture's bytes were already there: the tests own it
    Foreign, // another file of that name: left as it is, not the tests'
    Failed,  // the copy failed: nothing of the tests' is there
};

// Whether the tests own `target` after placing it, and must remove it.
inline bool ownsPlacedFixture(FixturePlacement placement)
{
    return placement == FixturePlacement::Copied || placement == FixturePlacement::Reused;
}

inline bool sameFileBytes(const std::filesystem::path &a, const std::filesystem::path &b)
{
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb)
        return false;
    const std::string da((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
    const std::string db((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
    return da == db;
}

// Puts `source` at `target`; `ec` holds the reason of a failed copy.
inline FixturePlacement placeFixtureFile(const std::filesystem::path &source, const std::filesystem::path &target,
                                         std::error_code &ec)
{
    ec.clear();
    if (std::filesystem::exists(target, ec))
        return sameFileBytes(source, target) ? FixturePlacement::Reused : FixturePlacement::Foreign;
    std::filesystem::copy_file(source, target, ec);
    return ec ? FixturePlacement::Failed : FixturePlacement::Copied;
}

// Removes `path`, trying again while something still holds it (on Windows
// the font cache can keep a just-unregistered font open for a moment) until
// `patience` has passed. True when the file is gone; `ec` holds the last
// reason otherwise.
inline bool removeFixtureFile(const std::filesystem::path &path, std::chrono::milliseconds patience,
                              std::error_code &ec)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        ec.clear();
        std::filesystem::remove(path, ec);
        std::error_code exists;
        if (!ec && !std::filesystem::exists(path, exists))
            return true;
        if (std::chrono::steady_clock::now() - start >= patience)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace hikari::testing
