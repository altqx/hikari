#pragma once

// DirectShow's audio renderer needs an audio output device and each codec a system decoder;
// GitHub's Windows runners (no sound card) may lack either.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dshow.h>
#include <mmsystem.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace hikari::testing {

inline std::string hresultText(HRESULT hr)
{
    char text[16];
    std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(hr));
    return text;
}

// A FOURCC's media subtype (FOURCCMap): {fourcc-0000-0010-8000-00AA00389B71}.
inline GUID fourccSubtype(DWORD fourcc)
{
    return GUID{fourcc, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
}

inline constexpr DWORD fourcc(char a, char b, char c, char d)
{
    return DWORD(BYTE(a)) | (DWORD(BYTE(b)) << 8) | (DWORD(BYTE(c)) << 16) | (DWORD(BYTE(d)) << 24);
}

struct DirectShowGaps {
    std::string audioRenderer;                               // empty: it can be created
    std::string filterMapper;                                // empty: it can be created
    std::vector<std::pair<std::string, std::string>> codecs; // (name, what is missing), missing ones only

    // What a graph over video in `codecNames` lacks here; empty when nothing.
    std::string missing(const std::vector<std::string> &codecNames) const
    {
        std::string out;
        const auto add = [&out](const std::string &what) {
            if (!what.empty())
                out += (out.empty() ? "" : "; ") + what;
        };
        add(audioRenderer);
        add(filterMapper);
        for (const auto &[name, what] : codecs)
            for (const auto &wanted : codecNames)
                if (name == wanted)
                    add(what);
        return out;
    }
    std::string all() const
    {
        std::vector<std::string> names;
        for (const auto &[name, what] : codecs)
            names.push_back(name);
        return missing(names);
    }
};

inline DirectShowGaps directShowGaps(const std::vector<std::pair<DWORD, const char *>> &codecs)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    DirectShowGaps gaps;

    IBaseFilter *audio = nullptr;
    const HRESULT created =
        CoCreateInstance(CLSID_DSoundRender, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&audio));
    if (audio)
        audio->Release();
    if (FAILED(created))
        gaps.audioRenderer = "DirectShow's audio renderer cannot be created (" + hresultText(created) +
                             (created == VFW_E_NO_AUDIO_HARDWARE ? " VFW_E_NO_AUDIO_HARDWARE" : "") + ", " +
                             std::to_string(waveOutGetNumDevs()) +
                             " audio output devices), and legacy's graph adds it to every video";

    // The decoders the graph's intelligent connect could insert (DMOs included).
    IFilterMapper2 *mapper = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mapper)))) {
        for (const auto &[code, name] : codecs) {
            const GUID types[2] = {MEDIATYPE_Video, fourccSubtype(code)};
            IEnumMoniker *found = nullptr;
            bool any = false;
            if (SUCCEEDED(mapper->EnumMatchingFilters(&found, 0, TRUE, MERIT_DO_NOT_USE + 1, TRUE, 1, types, nullptr,
                                                      nullptr, FALSE, FALSE, 0, nullptr, nullptr, nullptr)) &&
                found) {
                IMoniker *filter = nullptr;
                any = found->Next(1, &filter, nullptr) == S_OK;
                if (filter)
                    filter->Release();
                found->Release();
            }
            if (!any)
                gaps.codecs.emplace_back(name, std::string("no DirectShow decoder takes ") + name +
                                                   " video (IFilterMapper2 lists none)");
        }
        mapper->Release();
    } else {
        gaps.filterMapper = "the DirectShow filter mapper cannot be created";
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return gaps;
}

// What this host lacks for a graph over video in `codecs` (FOURCC, name); empty when nothing.
inline std::string directShowGraphMissing(const std::vector<std::pair<DWORD, const char *>> &codecs)
{
    return directShowGaps(codecs).all();
}

}

#endif
