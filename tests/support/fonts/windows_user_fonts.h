#pragma once

// The Windows font fixture (Y8W): the generated CC0 fixtures installed for
// the current user for the length of one test process, the way Windows'
// per-user install makes a font a system font.
//
// - A value under HKCU\Software\Microsoft\Windows NT\CurrentVersion\Fonts
//   names each file, so DirectWrite's system collection lists it
//   (FontService::systemFaces, the collector's availability check and the
//   provider path of a stream font).
// - AddFontResourceEx(FR_PRIVATE) loads it into this process's GDI font
//   table, where libass's DirectWrite provider looks a family up
//   (EnumFontFamilies, then CreateFontFaceFromHdc: "directwrite (with GDI)").
//
// SetUp waits until DirectWrite reports every family, asking it to check for
// changes; TearDown removes both again, so nothing stays installed.

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

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
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

    void SetUp() override
    {
        HKEY key = nullptr;
        ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, kFontsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr),
                  ERROR_SUCCESS);
        for (const auto &name : m_files) {
            const std::wstring path = (m_directory / name).make_preferred().wstring();
            const std::wstring value = valueName(name);
            const LSTATUS set =
                RegSetValueExW(key, value.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(path.c_str()),
                               DWORD((path.size() + 1) * sizeof(wchar_t)));
            EXPECT_EQ(set, ERROR_SUCCESS) << std::filesystem::path(path).string();
            if (set == ERROR_SUCCESS)
                m_registered.push_back(value);
            const int added = AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
            EXPECT_GT(added, 0) << std::filesystem::path(path).string();
            if (added > 0)
                m_loaded.push_back(path);
        }
        RegCloseKey(key);

        IDWriteFactory *factory = nullptr;
        ASSERT_TRUE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                  reinterpret_cast<IUnknown **>(&factory))));
        const auto start = std::chrono::steady_clock::now();
        bool listed = false;
        while (!listed && std::chrono::steady_clock::now() - start < std::chrono::seconds(30)) {
            IDWriteFontCollection *collection = nullptr;
            if (SUCCEEDED(factory->GetSystemFontCollection(&collection, TRUE)) && collection) {
                listed = true;
                for (const auto &family : m_families) {
                    UINT32 index = 0;
                    BOOL exists = FALSE;
                    listed = listed && SUCCEEDED(collection->FindFamilyName(family.c_str(), &index, &exists)) && exists;
                }
                collection->Release();
            }
            if (!listed)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        factory->Release();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::fprintf(stderr, "Windows user fonts: %zu files from %s, DirectWrite listed them after %lld ms\n",
                     m_files.size(), m_directory.string().c_str(), static_cast<long long>(ms));
        ASSERT_TRUE(listed) << "DirectWrite's system collection never listed the per-user fixture fonts";
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
};

} // namespace hikari::testing

#endif
