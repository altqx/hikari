#pragma once

// W2: the optional CSRI subtitle renderer adapter for the SubtitleRenderer
// port, Windows only in the application. The package ships one CSRI renderer,
// VSFilterMod (xy-VSFilter was dropped, user 2026-10-14); the adapter takes
// any renderer in the Csri folder, as legacy did. It reproduces the
// legacy CSRI path at 20d647c4:
// - CsriMod.cpp: the renderer list is every library under the program's Csri
//   folder (csrilib_os_init, csrilib_enum_dir), each library adding its
//   default renderer at the front of the list (csrilib_rend_initadd), so the
//   last one loaded is the default;
// - SubtitlesVSFilter.cpp: the renderer the VSFILTER_INSTANCE name selects,
//   else the default (GetVSFilter); the script opened from memory as UTF-8
//   (ParseInstance); the frame format requested when the size is known,
//   BGRA with BGR_ as the fallback (SetVideoParameters); the overlay drawn
//   onto a cleared frame at the time in seconds, whole (DrawOverlay); one
//   lock around every CSRI call (s_CsriMutex).
// A renderer that takes BGRA writes straight alpha into it (VSFilter's
// MemSubPic.cpp AlphaBltOther, MSP_RGBA), which legacy blended with
// D3DBLEND_SRCALPHA (RendererFFMS2::DrawOverlay); the port's overlay is
// premultiplied, so the colours are multiplied by their alpha here. A renderer that takes only
// BGR_ (VSFilterMod) blends into the frame and leaves no alpha; legacy drew
// it onto the RGB32 video frame (GetFrameWithSubs without the overlay), so
// the same instant is drawn onto black and onto white and the overlay is
// what the two differ by: each pixel's colour and its coverage.
// The script opened starts with a UTF-8 byte order mark, as GetVisible's text
// did (SubsGridBase.cpp:1804-1805); a renderer's exception on open is a script
// it cannot open.
// VSFilter resolves fonts through GDI: system fonts, the script's [Fonts]
// section, and the snapshot's font leases, which each context adds to the
// process privately while it lives (AddFontMemResourceEx; legacy loaded its
// external fonts folder with AddFontResourceExW(FR_PRIVATE), FontEnumerator.cpp
// LoadExternalFontsToProcess, so the in-process VSFilter saw them). A reload
// of the fonts (application::reloadSubtitleFonts: legacy's font watcher
// removed every external font from the process, added the folder's fonts
// again and reopened each tab's subtitles) adds the next context's leases
// anew, those the previous context had added as well, and the previous
// context's registrations go.

#include "hikari/application/subtitle_render.h"
#include "hikari/backends/overlay_blend.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends {

namespace csri {

// The CSRI ABI (csri.h, "common subtitle renderer interface", David Lamparter, 2007).
enum Pixfmt : int {
    BGRA = 2,      // CSRI_F_BGRA
    BGR_ = 0x102,  // CSRI_F_BGR_, Windows "RGB32"
};
struct Fmt {
    int pixfmt;
    unsigned width;
    unsigned height;
};
struct Frame {
    int pixfmt;
    unsigned char *planes[4];
    std::ptrdiff_t strides[4];
};
struct Info {
    const char *name;
    const char *specific;
    const char *longname;
    const char *author;
    const char *copyright;
};

// One renderer: a library's default renderer and the entry points it
// exports (legacy csri_wrap_rend).
struct Renderer {
    void *rend = nullptr;
    void *(*openMem)(void *rend, const void *data, std::size_t length, void *flags) = nullptr;
    void (*close)(void *inst) = nullptr;
    int (*requestFmt)(void *inst, const Fmt *fmt) = nullptr;
    void (*render)(void *inst, Frame *frame, double time) = nullptr;
    const Info *info = nullptr;
};

} // namespace csri

// The loaded renderers (legacy wraprends), shared by every adapter instance.
class CsriRenderers {
public:
    using Log = std::function<void(const std::string &)>;
    // In list order: the first is the default.
    explicit CsriRenderers(std::vector<csri::Renderer> renderers, std::vector<std::shared_ptr<void>> libraries = {});
    ~CsriRenderers();
#ifdef _WIN32
    // csrilib_os_init: every library under `folder` (the program's Csri
    // folder), recursively; what cannot be loaded is logged silently.
    static std::shared_ptr<CsriRenderers> load(const std::filesystem::path &folder, const Log &log);
#endif
    // The renderers' names, as SubtitlesVSFilter::GetProviders lists them.
    std::vector<std::string> names() const;
    // GetVSFilter: the renderer named `name`, else the default; null when
    // there is none.
    const csri::Renderer *select(std::string_view name) const;
    bool empty() const { return m_renderers.empty(); }
    // VSFilter is not safe to call from two threads at once.
    std::recursive_mutex &mutex() { return m_mutex; }

    // Adds a lease's font to the process for GDI, privately; the handle
    // removes it. The default does that on Windows (AddFontMemResourceEx) and
    // nothing elsewhere; null when the font was not added.
    using FontLoader = std::function<std::shared_ptr<void>(const application::FontLease &)>;
    void setFontLoader(FontLoader loader) { m_fontLoader = std::move(loader); }
    const FontLoader &fontLoader() const { return m_fontLoader; }
    std::shared_ptr<void> loadFont(const application::FontLease &font) const;

private:
    FontLoader m_fontLoader;
    std::vector<csri::Renderer> m_renderers;
    std::vector<std::shared_ptr<void>> m_libraries; // keeps the modules loaded
    std::recursive_mutex m_mutex;
};

class CsriRenderer : public application::SubtitleRendererPort {
public:
    // `name`: the VSFILTER_INSTANCE setting. `log` takes legacy's silent
    // messages ("Cannot initialize CSRI." and the others).
    CsriRenderer(std::shared_ptr<CsriRenderers> renderers, std::string name, CsriRenderers::Log log = {});
    ~CsriRenderer() override;

    std::expected<std::uint64_t, application::RenderError> prepare(application::RenderSnapshot snapshot) override;
    std::expected<application::OverlayFrame, application::RenderError> render(core::DocumentTime time, int width,
                                                                             int height) override;

    const std::string &name() const { return m_name; }
    // The renderer the last prepare selected (its csri_info name), empty without one.
    std::string selectedName() const;

private:
    void closeInstance();
    std::shared_ptr<CsriRenderers> m_renderers;
    std::string m_name;
    CsriRenderers::Log m_log;
    const csri::Renderer *m_renderer = nullptr;
    void *m_instance = nullptr;
    int m_pixfmt = csri::BGRA;
    int m_width = 0, m_height = 0;
    std::uint64_t m_generation = 0;
    // The context's leased fonts, added to the process until a prepare
    // without them, a prepare after a fonts reload, or the adapter's end, by
    // the lease's bytes. The entry holds the bytes, so no other font's buffer
    // can take their address while the entry stands for them.
    struct AddedFont {
        std::shared_ptr<const std::vector<std::byte>> bytes;
        std::shared_ptr<void> handle;
    };
    std::vector<AddedFont> m_fonts;
    std::uint64_t m_fontReloads = 0; // application::subtitleFontsReloads() when m_fonts were added
    // The second frame a BGR_ renderer draws onto (white), kept between frames.
    std::vector<std::uint8_t> m_white;
    // The overlay buffers, shared with the frames; and what the shown one has drawn.
    OverlayBuffers m_buffers;
    application::PixelRect m_bounds;
};

} // namespace hikari::backends
