#pragma once

// W1: whether this Windows host can build legacy's DirectShow graph for the
// test fixtures. Legacy's graph (DShowPlayer::OpenFile, which the adapter
// keeps) adds DirectShow's audio renderer to every video, so a host without
// an audio output device opens no file with it (the open fails as legacy's
// did, "Cannot create audio renderer instance"); and each fixture needs a
// system decoder for its codec. The GitHub Windows runners (Windows Server,
// no sound card) may lack either; the Winix VM has both. A test that needs
// the graph skips naming what is missing instead of failing or running into
// its timeout.

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

// What this host lacks for legacy's graph over video in `codecs` (FOURCC,
// name); empty when nothing is missing.
inline std::string directShowGraphMissing(const std::vector<std::pair<DWORD, const char *>> &codecs)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string missing;
    const auto add = [&missing](const std::string &what) { missing += (missing.empty() ? "" : "; ") + what; };

    IBaseFilter *audio = nullptr;
    const HRESULT created =
        CoCreateInstance(CLSID_DSoundRender, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&audio));
    if (audio)
        audio->Release();
    if (FAILED(created))
        add("DirectShow's audio renderer cannot be created (" + hresultText(created) +
            (created == VFW_E_NO_AUDIO_HARDWARE ? " VFW_E_NO_AUDIO_HARDWARE" : "") + ", " +
            std::to_string(waveOutGetNumDevs()) + " audio output devices), and legacy's graph adds it to every video");

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
                add(std::string("no DirectShow decoder takes ") + name + " video (IFilterMapper2 lists none)");
        }
        mapper->Release();
    } else {
        add("the DirectShow filter mapper cannot be created");
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return missing;
}

} // namespace hikari::testing

#endif
