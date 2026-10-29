#pragma once

// DirectWrite lists a per-user font only from the user font folder plus an HKCU Fonts value; libass's
// DirectWrite provider looks families up in the process's GDI table, hence AddFontResourceEx(FR_PRIVATE).

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
    // `files` are names inside `directory`; `families` must be listed by DirectWrite before the tests start.
    WindowsUserFonts(std::filesystem::path directory, std::vector<std::wstring> files,
                     std::vector<std::wstring> families)
        : m_directory(std::move(directory)), m_files(std::move(files)), m_families(std::move(families))
    {
    }

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
        const auto listed = std::chrono::steady_clock::now();
        // Right after a reinstall the font cache lists families before their faces open from the new files.
        std::vector<fs::path> unserved = m_owned;
        while (!unserved.empty() && std::chrono::steady_clock::now() - start < std::chrono::seconds(30)) {
            const std::vector<std::wstring> served = servedFiles(factory);
            std::erase_if(unserved, [&served](const fs::path &file) {
                for (const auto &path : served)
                    if (CompareStringOrdinal(path.c_str(), -1, file.c_str(), -1, TRUE) == CSTR_EQUAL)
                        return true;
                return false;
            });
            if (!unserved.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        factory->Release();
        const auto now = std::chrono::steady_clock::now();
        const auto ms = [&](auto at) {
            return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(at - start).count());
        };
        std::fprintf(stderr,
                     "Windows user fonts: %zu files installed in %s (%zu left by an earlier run), DirectWrite "
                     "listed %zu of %zu families after %lld ms and served %zu of %zu files after %lld ms\n",
                     m_owned.size(), folder.string().c_str(), m_reused, m_families.size() - missing.size(),
                     m_families.size(), ms(listed), m_owned.size() - unserved.size(), m_owned.size(), ms(now));
        for (const auto &family : missing)
            ADD_FAILURE() << "DirectWrite's system collection never listed " << fs::path(family).string();
        for (const auto &file : unserved)
            ADD_FAILURE() << "DirectWrite never opened a face from " << file.string();
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
    static std::vector<std::wstring> servedFiles(IDWriteFactory *factory)
    {
        std::vector<std::wstring> out;
        IDWriteFontCollection *collection = nullptr;
        if (FAILED(factory->GetSystemFontCollection(&collection, TRUE)) || !collection)
            return out;
        for (UINT32 f = 0; f < collection->GetFontFamilyCount(); ++f) {
            IDWriteFontFamily *family = nullptr;
            if (FAILED(collection->GetFontFamily(f, &family)))
                continue;
            for (UINT32 i = 0; i < family->GetFontCount(); ++i) {
                IDWriteFont *font = nullptr;
                IDWriteFontFace *face = nullptr;
                if (SUCCEEDED(family->GetFont(i, &font)) && SUCCEEDED(font->CreateFontFace(&face))) {
                    UINT32 count = 1;
                    IDWriteFontFile *file = nullptr;
                    const void *key = nullptr;
                    UINT32 keySize = 0;
                    IDWriteFontFileLoader *loader = nullptr;
                    IDWriteLocalFontFileLoader *local = nullptr;
                    UINT32 length = 0;
                    if (SUCCEEDED(face->GetFiles(&count, &file)) && file &&
                        SUCCEEDED(file->GetReferenceKey(&key, &keySize)) && SUCCEEDED(file->GetLoader(&loader)) &&
                        SUCCEEDED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                                         reinterpret_cast<void **>(&local))) &&
                        SUCCEEDED(local->GetFilePathLengthFromKey(key, keySize, &length))) {
                        std::wstring path(length + 1, L'\0');
                        if (SUCCEEDED(local->GetFilePathFromKey(key, keySize, path.data(), length + 1))) {
                            path.resize(length);
                            out.push_back(std::move(path));
                        }
                    }
                    if (local)
                        local->Release();
                    if (loader)
                        loader->Release();
                    if (file)
                        file->Release();
                }
                if (face)
                    face->Release();
                if (font)
                    font->Release();
            }
            family->Release();
        }
        collection->Release();
        return out;
    }

    static constexpr const wchar_t *kFontsKey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";

    static std::wstring valueName(const std::wstring &file) { return L"Hikari test font " + file + L" (TrueType)"; }

    std::filesystem::path m_directory;
    std::vector<std::wstring> m_files;
    std::vector<std::wstring> m_families;
    std::vector<std::wstring> m_registered;
    std::vector<std::wstring> m_loaded;
    std::vector<std::filesystem::path> m_owned;
    std::size_t m_reused = 0;
};

}

#endif
