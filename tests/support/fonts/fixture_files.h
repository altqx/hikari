#pragma once

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

inline FixturePlacement placeFixtureFile(const std::filesystem::path &source, const std::filesystem::path &target,
                                         std::error_code &ec)
{
    ec.clear();
    if (std::filesystem::exists(target, ec))
        return sameFileBytes(source, target) ? FixturePlacement::Reused : FixturePlacement::Foreign;
    std::filesystem::copy_file(source, target, ec);
    return ec ? FixturePlacement::Failed : FixturePlacement::Copied;
}

// Retries until `patience` passes: Windows' font cache can hold a just-unregistered font open for a moment.
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

}
