#pragma once

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

// Mirrors the CSRI ABI (csri.h): layouts and values must match it.
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

struct Renderer {
    void *rend = nullptr;
    void *(*openMem)(void *rend, const void *data, std::size_t length, void *flags) = nullptr;
    void (*close)(void *inst) = nullptr;
    int (*requestFmt)(void *inst, const Fmt *fmt) = nullptr;
    void (*render)(void *inst, Frame *frame, double time) = nullptr;
    const Info *info = nullptr;
};

} // namespace csri

class CsriRenderers {
public:
    using Log = std::function<void(const std::string &)>;
    // In list order: the first is the default.
    explicit CsriRenderers(std::vector<csri::Renderer> renderers, std::vector<std::shared_ptr<void>> libraries = {});
    ~CsriRenderers();
#ifdef _WIN32
    // Every library under `folder`, recursively; failures go to `log`.
    static std::shared_ptr<CsriRenderers> load(const std::filesystem::path &folder, const Log &log);
#endif
    std::vector<std::string> names() const;
    // The renderer named `name`, else the default; null when there is none.
    const csri::Renderer *select(std::string_view name) const;
    bool empty() const { return m_renderers.empty(); }
    // VSFilter is not safe to call from two threads at once.
    std::recursive_mutex &mutex() { return m_mutex; }

    // Adds a lease's font privately for GDI until the handle goes; null when not added.
    using FontLoader = std::function<std::shared_ptr<void>(const application::FontLease &)>;
    void setFontLoader(FontLoader loader) { m_fontLoader = std::move(loader); }
    const FontLoader &fontLoader() const { return m_fontLoader; }
    std::shared_ptr<void> loadFont(const application::FontLease &font) const;

private:
    FontLoader m_fontLoader;
    std::vector<csri::Renderer> m_renderers;
    std::vector<std::shared_ptr<void>> m_libraries;
    std::recursive_mutex m_mutex;
};

class CsriRenderer : public application::SubtitleRendererPort {
public:
    // `name`: the VSFILTER_INSTANCE setting.
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
    struct AddedFont {
        // MUST KILL: held only so a freed buffer's address can never match a new lease.
        std::shared_ptr<const std::vector<std::byte>> bytes;
        std::shared_ptr<void> handle;
    };
    std::vector<AddedFont> m_fonts;
    std::uint64_t m_fontReloads = 0;
    std::vector<std::uint8_t> m_white;
    OverlayBuffers m_buffers;
    application::PixelRect m_bounds;
};

} // namespace hikari::backends
