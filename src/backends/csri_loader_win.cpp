// W2: the CSRI renderer list on Windows, as legacy CsriMod.cpp builds it
// (20d647c4: csrilib_os_init, csrilib_enum_dir, csrilib_load, csrilib_do_load).

#include "hikari/backends/csri_renderer.h"

#include <windows.h>

#include <string>

namespace hikari::backends {

namespace {

// get_errstr: the system's text for GetLastError, without its last newline.
std::string errorText()
{
    char msg[2048];
    const DWORD err = GetLastError();
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0, msg,
                        sizeof(msg), nullptr))
        return "Unknown Error";
    std::string text(msg);
    if (!text.empty() && text.back() == '\n')
        text.pop_back();
    return text;
}

std::string utf8(const std::wstring &w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

struct Loader {
    const CsriRenderers::Log &log;
    std::vector<csri::Renderer> renderers;
    std::vector<std::shared_ptr<void>> libraries;

    void say(const std::string &text) const
    {
        if (log)
            log(text);
    }

    template <typename F> bool map(HMODULE module, const char *symbol, F &out, const std::wstring &file)
    {
        out = reinterpret_cast<F>(reinterpret_cast<void *>(GetProcAddress(module, symbol)));
        if (!out) {
            say(utf8(file) + " symbol " + symbol + " not found " + errorText());
            return false;
        }
        return true;
    }

    // csrilib_do_load
    void loadLibrary(const std::wstring &file)
    {
        HMODULE module = LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            say("LoadLibraryEx(\"" + utf8(file) + ", " + errorText() + "\") failed: ");
            return;
        }
        if (GetProcAddress(module, "csri_library")) {
            // a CSRI wrapper library, not a renderer
            say("ignoring library(\"" + utf8(file) + ", " + errorText() + "\")");
            FreeLibrary(module);
            return;
        }
        void *(*queryExt)(void *, const char *) = nullptr;
        void *(*openFile)(void *, const char *, void *) = nullptr;
        csri::Renderer r;
        const csri::Info *(*rendererInfo)(void *) = nullptr;
        void *(*rendererDefault)() = nullptr;
        void *(*rendererNext)(void *) = nullptr;
        if (!map(module, "csri_query_ext", queryExt, file) || !map(module, "csri_open_file", openFile, file) ||
            !map(module, "csri_open_mem", r.openMem, file) || !map(module, "csri_close", r.close, file) ||
            !map(module, "csri_request_fmt", r.requestFmt, file) || !map(module, "csri_render", r.render, file) ||
            !map(module, "csri_renderer_info", rendererInfo, file) ||
            !map(module, "csri_renderer_default", rendererDefault, file) ||
            !map(module, "csri_renderer_next", rendererNext, file)) {
            FreeLibrary(module);
            return;
        }
        // Only the library's default renderer is added, at the front.
        r.rend = rendererDefault();
        r.info = rendererInfo(r.rend);
        renderers.insert(renderers.begin(), r);
        // Legacy never unloads a renderer library (csri_close_renderer frees
        // only its list), so neither does this.
        libraries.emplace_back(static_cast<void *>(module), [](void *) {});
    }

    // csrilib_load
    void load(const std::wstring &path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return;
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            enumerate(path);
            return;
        }
        loadLibrary(path);
    }

    // csrilib_enum_dir: every entry whose name does not start with '.'.
    void enumerate(const std::wstring &dir)
    {
        WIN32_FIND_DATAW data;
        HANDLE found = FindFirstFileW((dir + L"\\*").c_str(), &data);
        if (found == INVALID_HANDLE_VALUE) {
            say("ignoring directory " + utf8(dir) + " : " + errorText());
            return;
        }
        do {
            if (data.cFileName[0] == L'.')
                continue;
            load(dir + L"\\" + data.cFileName);
        } while (FindNextFileW(found, &data));
        FindClose(found);
    }
};

} // namespace

std::shared_ptr<CsriRenderers> CsriRenderers::load(const std::filesystem::path &folder, const Log &log)
{
    Loader loader{log, {}, {}};
    loader.enumerate(folder.wstring());
    return std::make_shared<CsriRenderers>(std::move(loader.renderers), std::move(loader.libraries));
}

} // namespace hikari::backends
