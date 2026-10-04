#include "hikari/application/legacy_dir.h"

#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace hikari::application::legacy_dir {

namespace fs = std::filesystem;

bool matchWild(std::u32string_view pat, std::u32string_view text, bool dotSpecial)
{
    // A direct port of wxMatchWild (src/common/filefn.cpp); positions past
    // the end read as the terminating NUL.
    if (text.empty())
        return pat.empty();
    const auto at = [](std::u32string_view s, std::size_t i) { return i < s.size() ? s[i] : U'\0'; };
    std::size_t m = 0, n = 0, ma = 0, na = 0;
    bool haveMa = false;
    bool just = false;
    if (dotSpecial && text.front() == U'.')
        return false;
    for (;;) {
        if (at(pat, m) == U'*') {
            ma = ++m;
            haveMa = true;
            na = n;
            just = true;
        } else if (at(pat, m) == U'?') {
            m++;
            if (!at(text, n++))
                return false;
        } else {
            bool notMatched = false;
            if (at(pat, m) == U'\\') {
                m++;
                if (!at(pat, m))
                    return false; // quoting nothing
            }
            if (!at(pat, m)) {
                if (!at(text, n))
                    return true;
                if (just)
                    return true;
                just = false;
                notMatched = true;
            }
            if (!notMatched) {
                just = false;
                if (at(pat, m) == at(text, n)) {
                    m++;
                    n++;
                    continue;
                }
            }
            // not_matched:
            if (!at(text, n))
                return false;
            if (!haveMa)
                return false;
            m = ma;
            n = ++na;
        }
    }
}

#ifdef _WIN32

std::optional<std::vector<fs::path>> entries(const fs::path &folder, std::u16string_view filespec, int flags)
{
    // wxDir::Open: wxDirExists.
    const DWORD folderAttributes = GetFileAttributesW(folder.c_str());
    if (folderAttributes == INVALID_FILE_ATTRIBUTES || !(folderAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return std::nullopt;
    std::wstring spec = folder.native();
    if (spec.empty() || (spec.back() != L'\\' && spec.back() != L'/'))
        spec += L'\\';
    const std::wstring filter(filespec.begin(), filespec.end());
    spec += filter.empty() ? std::wstring(L"*.*") : filter;

    std::vector<fs::path> out;
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileW(spec.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return out; // ERROR_FILE_NOT_FOUND / ERROR_NO_MORE_FILES: no such files
    do {
        // CheckFoundMatch: the system matched the long or the 8.3 name; wx
        // keeps only a long name PathMatchSpec matches.
        if (!filter.empty() && !PathMatchSpecW(data.cFileName, filter.c_str()))
            continue;
        const wchar_t *name = data.cFileName;
        if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0')))
            continue;
        const DWORD attributes = data.dwFileAttributes;
        const bool isDir = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!(flags & Files) && !isDir)
            continue;
        if (!(flags & Dirs) && isDir)
            continue;
        if (!(flags & Hidden) && (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            continue;
        out.push_back(folder / name);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return out;
}

#else

namespace {

// wxConvFileName (wxConvWhateverWorks): UTF-8, else byte by byte.
std::u32string decodeName(std::string_view bytes)
{
    const auto latin1 = [&] {
        std::u32string out;
        for (const char c : bytes)
            out += static_cast<char32_t>(static_cast<unsigned char>(c));
        return out;
    };
    std::u32string out;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        const int extra = b < 0x80 ? 0 : (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0 || i + static_cast<std::size_t>(extra) >= bytes.size())
            return latin1();
        char32_t c = extra == 0 ? b : extra == 1 ? (b & 0x1F) : extra == 2 ? (b & 0x0F) : (b & 0x07);
        for (int k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(bytes[i + static_cast<std::size_t>(k)]);
            if ((next & 0xC0) != 0x80)
                return latin1();
            c = (c << 6) | (next & 0x3F);
        }
        out += c;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

bool isFolder(const fs::path &path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::u32string toUtf32(std::u16string_view s)
{
    std::u32string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char16_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            out += static_cast<char32_t>(0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00));
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace

std::optional<std::vector<fs::path>> entries(const fs::path &folder, std::u16string_view filespec, int flags)
{
    if (!isFolder(folder))
        return std::nullopt;
    DIR *dir = ::opendir(folder.c_str());
    if (!dir)
        return std::nullopt;
    const std::u32string pattern = toUtf32(filespec);
    std::vector<fs::path> out;
    while (const dirent *entry = ::readdir(dir)) {
        const std::string_view name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        const fs::path path = folder / name;
        const bool isDir = isFolder(path);
        if (!(flags & Files) && !isDir)
            continue;
        if (!(flags & Dirs) && isDir)
            continue;
        const bool matches = pattern.empty() ? ((flags & Hidden) || name.front() != '.')
                                             : matchWild(pattern, decodeName(name), !(flags & Hidden));
        if (matches)
            out.push_back(path);
    }
    ::closedir(dir);
    return out;
}

#endif

} // namespace hikari::application::legacy_dir
