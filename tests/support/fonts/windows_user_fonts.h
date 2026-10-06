#pragma once

// The Windows font fixture (Y8W): the generated CC0 fixtures installed for
// the current user for the length of one test process, the way Windows'
// per-user install (the shell's "Install") makes a font a system font.
//
// - Each file is copied into the user's font folder
//   (%LOCALAPPDATA%\Microsoft\Windows\Fonts) under its own name, and a value
//   under HKCU\Software\Microsoft\Windows NT\CurrentVersion\Fonts names it,
//   so DirectWrite's system collection lists it (FontService::systemFaces,
//   the collector's availability check and the provider path of a stream
//   font). A file of that name that is not the fixture is never replaced;
//   one with the fixture's bytes, left by a run that never reached its
//   TearDown, is the tests' own and is removed with the rest
//   (fixture_files.h).
// - AddFontResourceEx(FR_PRIVATE) loads it into this process's GDI font
//   table, where libass's DirectWrite provider looks a family up
//   (EnumFontFamilies, then CreateFontFaceFromHdc: "directwrite (with GDI)").
//
// SetUp waits until DirectWrite reports every family, asking it to check for
// changes; TearDown removes all of it again, so nothing stays installed: it
// waits for the font cache to let go of each file and reports a file it
// could not remove as a failure.
// A failed install is a test failure, never a skip: every test still runs.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwrite.h>

#include <gtest/gtest.h>

#include "fixture_files.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace hikari::testing {

class WindowsUserFonts final : public ::testing::Environment {
public:
    // `files` are names inside `directory`; `families` are the GDI family
    // names DirectWrite must list before the tests start.
    WindowsUserFonts(std::filesystem::path directory, std::vector<std::wstring> files,
                     std::vector<std::wstring> families)
        : m_directory(std::move(directory)), m_files(std::move(files)), m_families(std::move(families))
    {
    }

    // Where the fixtures are installed: the user's font folder.
    static std::filesystem::path userFontFolder()
    {
        wchar_t local[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        return std::filesystem::path(local) / L"Microsoft" / L"Windows" / L"Fonts";
    }

    void SetUp() override
    {
        namespace fs = std::filesystem;
        const fs::path folder = userFontFolder();
        EXPECT_FALSE(folder.empty()) << "LOCALAPPDATA";
        std::error_code ec;
        fs::create_directories(folder, ec);

        HKEY key = nullptr;
        const LSTATUS opened =
            RegCreateKeyExW(HKEY_CURRENT_USER, kFontsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        EXPECT_EQ(opened, ERROR_SUCCESS) << "HKCU Fonts key";
        for (const auto &name : m_files) {
            const fs::path source = m_directory / name;
            const fs::path target = (folder / name).make_preferred();
            const FixturePlacement placed = placeFixtureFile(source, target, ec);
            if (placed == FixturePlacement::Foreign) {
                ADD_FAILURE() << target.string() << " exists and is not the fixture; it is left as it is";
                continue;
            }
            if (placed == FixturePlacement::Failed) {
                ADD_FAILURE() << "copying " << source.string() << ": " << ec.message();
                continue;
            }
            if (placed == FixturePlacement::Reused)
                ++m_reused;
            m_owned.push_back(target);
            const std::wstring path = target.wstring();
            if (opened == ERROR_SUCCESS) {
                const std::wstring value = valueName(name);
                const LSTATUS set =
                    RegSetValueExW(key, value.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(path.c_str()),
                                   DWORD((path.size() + 1) * sizeof(wchar_t)));
                EXPECT_EQ(set, ERROR_SUCCESS) << target.string();
                if (set == ERROR_SUCCESS)
                    m_registered.push_back(value);
            }
            const int added = AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
            EXPECT_GT(added, 0) << target.string();
            if (added > 0)
                m_loaded.push_back(path);
        }
        if (key)
            RegCloseKey(key);

        IDWriteFactory *factory = nullptr;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown **>(&factory)))) {
            ADD_FAILURE() << "DWriteCreateFactory";
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::wstring> missing = m_families;
        while (!missing.empty() && std::chrono::steady_clock::now() - start < std::chrono::seconds(30)) {
            IDWriteFontCollection *collection = nullptr;
            if (SUCCEEDED(factory->GetSystemFontCollection(&collection, TRUE)) && collection) {
                missing.clear();
                for (const auto &family : m_families) {
                    UINT32 index = 0;
                    BOOL exists = FALSE;
                    if (FAILED(collection->FindFamilyName(family.c_str(), &index, &exists)) || !exists)
                        missing.push_back(family);
                }
                collection->Release();
            }
            if (!missing.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        factory->Release();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::fprintf(stderr,
                     "Windows user fonts: %zu files installed in %s (%zu left by an earlier run), DirectWrite "
                     "listed %zu of %zu families after %lld ms\n",
                     m_owned.size(), folder.string().c_str(), m_reused, m_families.size() - missing.size(),
                     m_families.size(), static_cast<long long>(ms));
        for (const auto &family : missing)
            ADD_FAILURE() << "DirectWrite's system collection never listed " << fs::path(family).string();
    }

    void TearDown() override
    {
        for (const auto &path : m_loaded)
            RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
        m_loaded.clear();
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kFontsKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            for (const auto &value : m_registered)
                RegDeleteValueW(key, value.c_str());
            RegCloseKey(key);
        }
        m_registered.clear();
        // Unregistered, a file is no longer installed, but the font cache
        // can still hold it open for a moment.
        std::size_t removed = 0;
        for (const auto &path : m_owned) {
            std::error_code ec;
            if (removeFixtureFile(path, std::chrono::seconds(10), ec))
                ++removed;
            else
                ADD_FAILURE() << "could not remove " << path.string() << ": " << ec.message();
        }
        std::fprintf(stderr, "Windows user fonts: removed %zu of %zu files\n", removed, m_owned.size());
        m_owned.clear();
        m_reused = 0;
    }

private:
    static constexpr const wchar_t *kFontsKey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";

    // One value per file, recognisably the tests' own.
    static std::wstring valueName(const std::wstring &file) { return L"Hikari test font " + file + L" (TrueType)"; }

    std::filesystem::path m_directory;
    std::vector<std::wstring> m_files;
    std::vector<std::wstring> m_families;
    std::vector<std::wstring> m_registered;
    std::vector<std::wstring> m_loaded;
    // Copied or reused: the files TearDown removes.
    std::vector<std::filesystem::path> m_owned;
    std::size_t m_reused = 0;
};

} // namespace hikari::testing

#endif
