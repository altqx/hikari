// V3 (#182): the order VIDEO_PREVIOUS_FILE / VIDEO_NEXT_FILE walk a folder in,
// from the legacy code itself. VideoBox::NextFile (HikariSub/VideoBox.cpp:
// 682-739 at 20d647c4) is copied out of its file unchanged
// (extract_functions.py) and compiled against the pinned wxWidgets 3.3.3's
// wxbase, so its wxDir::GetAllFiles(path, &files, emptyString, wxDIR_FILES)
// is wx's own on this platform (src/unix/dir.cpp or src/msw/dir.cpp). The
// GUI it calls is stood in for by standins.h: every file opens.
//
// Usage: legacy_next_file_capture <names file> <folder>
//
// The names file has one entry per line, created in that order in <folder>
// (which must not exist yet): "file <name>", "dir <name>" or "hidden <name>"
// (a file with FILE_ATTRIBUTE_HIDDEN on Windows, a plain file elsewhere);
// '#' starts a comment. Names are UTF-8.
//
// It prints one JSON object: the platform, wx's version, the folder's file
// system, the entries as created, the folder as the system enumerates it
// (readdir or FindFirstFileW, every entry), NextFile's listing (its `files`
// member after the first call) and two walks: VIDEO_NEXT_FILE pressed from
// the listing's first video until NextFile seeks to the start instead of
// opening, and VIDEO_PREVIOUS_FILE from its last video likewise. Names are
// the entries' own (no folder).
#include "standins.h"

#include <wx/file.h>
#include <wx/init.h>
#include <wx/utils.h>

#include <clocale>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/vfs.h>
#endif

std::vector<std::string> g_calls;

namespace {
Notebook g_notebook;
}

Notebook *Notebook::GetTabs()
{
    return &g_notebook;
}

// The copied legacy definition.
#include "next_file.inc"

namespace {

struct Entry {
    std::string kind; // file, dir, hidden
    std::string name; // UTF-8
};

std::string utf8(const wxString &s)
{
    return std::string(s.ToUTF8().data());
}

std::string json(const std::string &s)
{
    std::string out = "\"";
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
            out += buffer;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string jsonList(const std::vector<std::string> &items)
{
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i)
        out += (i ? ", " : "") + json(items[i]);
    return out + "]";
}

std::string nameOf(const wxString &path)
{
    return utf8(wxFileName(path).GetFullName());
}

// The extensions NextFile opens (VideoBox.cpp:719-723), to choose where the
// walks start; the walks themselves are NextFile's.
bool isVideo(const wxString &path)
{
    const wxString ext = path.AfterLast(L'.').Lower();
    for (const wchar_t *e : {L"avi", L"mp4", L"mkv", L"ogm", L"wmv", L"asf", L"rmvb", L"rm", L"3gp", L"ts", L"m2ts",
                             L"mpg", L"mpeg"})
        if (ext == e)
            return true;
    return false;
}

std::vector<std::string> enumerate(const wxString &folder)
{
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAW data{};
    const wxString spec = folder + L"\\*";
    const HANDLE find = FindFirstFileW(spec.wc_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return out;
    do {
        const wxString name(data.cFileName);
        if (name != L"." && name != L"..")
            out.push_back(utf8(name));
    } while (FindNextFileW(find, &data));
    FindClose(find);
#else
    if (DIR *d = opendir(folder.fn_str())) {
        while (const dirent *e = readdir(d)) {
            const std::string name = e->d_name;
            if (name != "." && name != "..")
                out.push_back(utf8(wxString(e->d_name, *wxConvFileName)));
        }
        closedir(d);
    }
#endif
    return out;
}

std::string fileSystem(const wxString &folder)
{
#ifdef _WIN32
    wchar_t root[MAX_PATH] = {};
    wchar_t name[MAX_PATH] = {};
    if (GetVolumePathNameW(folder.wc_str(), root, MAX_PATH)
        && GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, name, MAX_PATH))
        return utf8(wxString(name));
    return "unknown";
#else
    struct statfs st {};
    if (statfs(folder.fn_str(), &st) != 0)
        return "unknown";
    switch (static_cast<unsigned long>(st.f_type)) {
    case 0x9123683EUL: return "btrfs";
    case 0xEF53UL: return "ext4";
    case 0x01021994UL: return "tmpfs";
    case 0x58465342UL: return "xfs";
    case 0x794C7630UL: return "overlayfs";
    case 0x2FC12FC1UL: return "zfs";
    case 0xF2F52010UL: return "f2fs";
    case 0x6969UL: return "nfs";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "0x%lx", static_cast<unsigned long>(st.f_type));
    return buffer;
#endif
}

// Presses VIDEO_NEXT_FILE (next) or VIDEO_PREVIOUS_FILE from `start` until
// NextFile seeks instead of opening: the files opened, in order, and what
// the last press did.
std::string walk(VideoBox &box, HikariSubFrame &frame, const wxString &start, bool next, std::size_t limit)
{
    frame.tab->VideoPath = start;
    std::vector<std::string> opened;
    std::vector<std::string> last;
    for (std::size_t press = 0; press < limit; ++press) {
        g_calls.clear();
        box.NextFile(next);
        bool openedOne = false;
        for (const std::string &call : g_calls)
            if (call.rfind("open ", 0) == 0) {
                opened.push_back(nameOf(wxString::FromUTF8(call.substr(5))));
                openedOne = true;
            }
        if (!openedOne) {
            last = g_calls;
            break;
        }
    }
    return "{\"start\": " + json(nameOf(start)) + ", \"opened\": " + jsonList(opened) + ", \"end\": " + jsonList(last) +
           "}";
}

} // namespace

int main(int argc, char **argv)
{
    std::setlocale(LC_ALL, "");
    wxInitializer init;
    if (!init.IsOk() || argc != 3) {
        std::fprintf(stderr, "usage: legacy_next_file_capture <names file> <folder>\n");
        return 2;
    }
    std::vector<Entry> entries;
    {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot read %s\n", argv[1]);
            return 2;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            const auto space = line.find(' ');
            if (space == std::string::npos)
                continue;
            entries.push_back({line.substr(0, space), line.substr(space + 1)});
        }
    }
    const wxString folder = wxString::FromUTF8(argv[2]);
    if (wxFileName::DirExists(folder) || !wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        std::fprintf(stderr, "%s exists or cannot be made\n", argv[2]);
        return 2;
    }
    const wxString absolute = wxFileName::DirName(folder).GetAbsolutePath();
    std::string created = "[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry &e = entries[i];
        const wxString path = wxFileName(absolute, wxString::FromUTF8(e.name)).GetFullPath();
        bool ok = false;
        if (e.kind == "dir") {
            ok = wxFileName::Mkdir(path);
        } else {
            wxFile f;
            ok = f.Create(path) && f.Write("x", 1) == 1;
            f.Close();
#ifdef _WIN32
            if (ok && e.kind == "hidden")
                ok = SetFileAttributesW(path.wc_str(), FILE_ATTRIBUTE_HIDDEN) != 0;
#endif
        }
        if (!ok) {
            std::fprintf(stderr, "cannot create %s\n", e.name.c_str());
            return 1;
        }
        created += (i ? ", " : "") + std::string("{\"kind\": ") + json(e.kind) + ", \"name\": " + json(e.name) + "}";
    }
    created += "]";

    TabPanel tab;
    HikariSubFrame frame;
    frame.tab = &tab;
    g_notebook.frame = &frame;
    VideoBox box;
    box.tab = &tab;

    // The listing: NextFile's own `files` after a press from any file of the folder.
    tab.VideoPath = wxFileName(absolute, L"probe-none").GetFullPath();
    g_calls.clear();
    box.NextFile(true);
    std::vector<std::string> listing;
    wxString firstVideo, lastVideo;
    for (const wxString &f : box.files) {
        listing.push_back(nameOf(f));
        if (isVideo(f)) {
            if (firstVideo.empty())
                firstVideo = f;
            lastVideo = f;
        }
    }
    // The listing again, through wxDir::GetAllFiles as NextFile calls it.
    wxArrayString direct;
    wxDir::GetAllFiles(absolute, &direct, emptyString, wxDIR_FILES);
    bool same = direct.GetCount() == box.files.GetCount();
    for (std::size_t i = 0; same && i < direct.GetCount(); ++i)
        same = direct[i] == box.files[i];

    box.actualFile = 0;
    const std::string forward = walk(box, frame, firstVideo, true, entries.size() + 2);
    box.actualFile = 0;
    const std::string backward = walk(box, frame, lastVideo, false, entries.size() + 2);

#ifdef _WIN32
    const char *platform = "windows";
#else
    const char *platform = "linux";
#endif
    std::cout << "{\n  \"probe\": \"next-file\",\n  \"platform\": " << json(platform)
              << ",\n  \"wx\": " << json(utf8(wxGetLibraryVersionInfo().GetVersionString()))
              << ",\n  \"filesystem\": " << json(fileSystem(absolute)) << ",\n  \"created\": " << created
              << ",\n  \"enumeration\": " << jsonList(enumerate(absolute)) << ",\n  \"listing\": " << jsonList(listing)
              << ",\n  \"listingIsGetAllFiles\": " << (same ? "true" : "false") << ",\n  \"next\": " << forward
              << ",\n  \"previous\": " << backward << "\n}\n";
    return 0;
}
