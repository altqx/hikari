#include "hikari/backends/libass_renderer.h"
#include "hikari/backends/overlay_blend.h"

extern "C" {
#include <ass/ass.h>
}

// Upstream libass bumps LIBASS_VERSION between releases too, so the fork is told by its feature
// macros; the version check only rules out an older fork.
#if !defined(LIBASS_HAS_TRACK_EVENTS_MODIFIED) || !defined(LIBASS_HAS_FRAME_ATLAS) \
    || !defined(LIBASS_HAS_FONT_DIAGNOSTICS)
#error "libass is not the altqx fork (ports/libass); a distribution libass cannot be used"
#endif
static_assert(LIBASS_VERSION >= 0x01705020, "the altqx libass fork is older than the pinned 0x01705020");

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <string_view>
#include <utility>

namespace hikari::backends {

using application::OverlayFrame;
using application::OverlayPixels;
using application::PixelRect;
using application::RenderError;

namespace {

constexpr int kMaxDimension = 16384;
constexpr std::size_t kTracks = 3;
constexpr std::size_t kTrackBytes = 128u << 20;

bool emptyRect(const PixelRect &r) { return r.width <= 0 || r.height <= 0; }

// xxHash64's round and merge; with the length it names a parsed track.
std::uint64_t scriptHash(const std::byte *data, std::size_t size)
{
    constexpr std::uint64_t p1 = 0x9E3779B185EBCA87ull, p2 = 0xC2B2AE3D27D4EB4Full, p3 = 0x165667B19E3779F9ull,
                            p4 = 0x85EBCA77C2B2AE63ull, p5 = 0x27D4EB2F165667C5ull;
    const auto rotl = [](std::uint64_t x, int r) { return (x << r) | (x >> (64 - r)); };
    const auto round = [&](std::uint64_t acc, std::uint64_t input) { return rotl(acc + input * p2, 31) * p1; };
    const auto word = [](const std::byte *p) {
        std::uint64_t w;
        std::memcpy(&w, p, 8);
        return w;
    };
    const std::byte *p = data, *end = data + size;
    std::uint64_t h;
    if (size >= 32) {
        std::uint64_t v1 = p1 + p2, v2 = p2, v3 = 0, v4 = 0 - p1;
        for (; end - p >= 32; p += 32) {
            v1 = round(v1, word(p));
            v2 = round(v2, word(p + 8));
            v3 = round(v3, word(p + 16));
            v4 = round(v4, word(p + 24));
        }
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
        for (std::uint64_t v : {v1, v2, v3, v4})
            h = (h ^ round(0, v)) * p1 + p4;
    } else {
        h = p5;
    }
    h += size;
    for (; end - p >= 8; p += 8)
        h = rotl(h ^ round(0, word(p)), 27) * p1 + p4;
    for (; p < end; ++p)
        h = rotl(h ^ (static_cast<std::uint64_t>(*p) * p5), 11) * p1;
    h ^= h >> 33;
    h *= p2;
    h ^= h >> 29;
    h *= p3;
    return h ^ (h >> 32);
}

}

struct LibassRenderer::Engine {
    struct Fonts {
        std::vector<std::pair<std::string, const void *>> leases;
        std::string defaultFamily;
        bool systemFonts = true;
        std::uint64_t reloads = 0;
        bool operator==(const Fonts &) const = default;
    };
    struct Track {
        std::uint64_t hash = 0;
        std::size_t size = 0;
        ASS_Track *track = nullptr;
    };

    ASS_Library *library = nullptr;
    ASS_Renderer *renderer = nullptr;
    Fonts key;
    std::vector<application::FontLease> fonts;
    std::vector<Track> tracks;
    int frameWidth = 0, frameHeight = 0;
    int storageWidth = 0, storageHeight = 0;

    static Fonts keyOf(const application::RenderSnapshot &snapshot)
    {
        Fonts key{{}, snapshot.defaultFamily, snapshot.systemFonts, application::subtitleFontsReloads()};
        for (const auto &font : snapshot.fonts)
            key.leases.emplace_back(font.family, font.bytes.get());
        return key;
    }

    ASS_Track *current() const { return tracks.empty() ? nullptr : tracks.front().track; }

    bool reuse(std::uint64_t hash, std::size_t size)
    {
        for (std::size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].hash == hash && tracks[i].size == size) {
                const auto at = tracks.begin() + static_cast<std::ptrdiff_t>(i);
                std::rotate(tracks.begin(), at, at + 1);
                return true;
            }
        return false;
    }

    std::vector<ASS_Track *> show(Track track)
    {
        tracks.insert(tracks.begin(), track);
        std::size_t bytes = 0, keep = 0;
        for (; keep < tracks.size(); ++keep) {
            bytes += tracks[keep].size;
            if (keep > 0 && (keep >= kTracks || bytes > kTrackBytes))
                break;
        }
        std::vector<ASS_Track *> gone;
        for (std::size_t i = keep; i < tracks.size(); ++i)
            gone.push_back(tracks[i].track);
        tracks.resize(keep);
        return gone;
    }

    ~Engine()
    {
        for (const Track &t : tracks)
            ass_free_track(t.track);
        if (renderer)
            ass_renderer_done(renderer);
        if (library)
            ass_library_done(library);
    }
};

LibassRenderer::LibassRenderer() = default;
LibassRenderer::~LibassRenderer() = default;

std::size_t LibassRenderer::parsedTracks() const
{
    std::lock_guard lock(m_mutex);
    return m_engine ? m_engine->tracks.size() : 0;
}

const void *LibassRenderer::engineIdentity() const
{
    std::lock_guard lock(m_mutex);
    return m_engine ? m_engine->renderer : nullptr;
}

// ass_read_memory writes the library only with extract_fonts on, which is never set, so a parse
// may run beside the rendering of another track. The fork's lazily rebuilt event index forbids
// two threads rendering one track at once.
std::expected<std::uint64_t, RenderError> LibassRenderer::prepare(application::RenderSnapshot snapshot)
{
    std::lock_guard prepareLock(m_prepare);
    Engine::Fonts key = Engine::keyOf(snapshot);
    const std::uint64_t hash = scriptHash(snapshot.script.data(), snapshot.script.size());
    const std::size_t size = snapshot.script.size();
    std::unique_ptr<Engine> fresh;
    Engine *engine = m_engine.get();
    if (engine && engine->key == key) {
        std::lock_guard lock(m_mutex);
        if (engine->reuse(hash, size)) {
            m_redraw = true;
            return ++m_generation;
        }
    } else {
        fresh = std::make_unique<Engine>();
        fresh->key = std::move(key);
        fresh->library = ass_library_init();
        if (!fresh->library)
            return std::unexpected(RenderError::BackendFailure);
        for (const auto &font : snapshot.fonts) {
            if (!font.bytes)
                continue;
            ass_add_font(fresh->library, font.family.c_str(),
                         const_cast<char *>(reinterpret_cast<const char *>(font.bytes->data())),
                         static_cast<int>(font.bytes->size()));
        }
        fresh->fonts = std::move(snapshot.fonts);
        fresh->renderer = ass_renderer_init(fresh->library);
        if (!fresh->renderer)
            return std::unexpected(RenderError::BackendFailure);
        ass_set_fonts(fresh->renderer, nullptr,
                      snapshot.defaultFamily.empty() ? nullptr : snapshot.defaultFamily.c_str(),
                      snapshot.systemFonts ? ASS_FONTPROVIDER_AUTODETECT : ASS_FONTPROVIDER_NONE, nullptr, 1);
        engine = fresh.get();
    }
    ASS_Track *track = ass_read_memory(engine->library,
                                       const_cast<char *>(reinterpret_cast<const char *>(snapshot.script.data())), size,
                                       nullptr);
    if (!track)
        return std::unexpected(RenderError::InvalidInput);
    std::vector<ASS_Track *> gone;
    std::unique_ptr<Engine> previous;
    std::uint64_t generation;
    {
        std::lock_guard lock(m_mutex);
        gone = engine->show(Engine::Track{hash, size, track});
        if (fresh)
            previous = std::exchange(m_engine, std::move(fresh));
        m_redraw = true;
        generation = ++m_generation;
    }
    for (ASS_Track *t : gone)
        ass_free_track(t);
    previous.reset();
    return generation;
}

namespace {

template <class Test> bool anyTag(std::string_view text, Test test)
{
    for (std::size_t open = text.find('{'); open != std::string_view::npos; open = text.find('{', open)) {
        const std::size_t close = text.find('}', open + 1);
        if (close == std::string_view::npos)
            return false;
        const std::string_view block = text.substr(open + 1, close - open - 1);
        for (std::size_t at = block.find('\\'); at != std::string_view::npos;) {
            const std::size_t next = block.find('\\', at + 1);
            if (test(block.substr(at + 1, next == std::string_view::npos ? next : next - at - 1)))
                return true;
            at = next;
        }
        open = close + 1;
    }
    return false;
}

double tagValue(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    double number = 0;
    std::from_chars(value.data(), value.data() + value.size(), number);
    return number;
}

bool blurs(std::string_view tag)
{
    if (tag.starts_with("blur"))
        return tagValue(tag.substr(4)) > 0;
    return tag.starts_with("be") && tagValue(tag.substr(2)) > 0;
}

// A drawing clip's arguments hold m or n moves; a rectangle clip's only numbers.
bool clipsToADrawing(std::string_view tag)
{
    if (tag.starts_with("i"))
        tag.remove_prefix(1);
    if (!tag.starts_with("clip("))
        return false;
    const std::size_t end = tag.find(')');
    const std::string_view args = tag.substr(5, end == std::string_view::npos ? end : end - 5);
    return std::ranges::any_of(args, [](char c) { return c == 'm' || c == 'M' || c == 'n' || c == 'N'; });
}

void count(application::RenderCost &cost, const ASS_Track &track, long long ms, const ASS_Image *images)
{
    cost.counted = true;
    for (int i = 0; i < track.n_events; ++i) {
        const ASS_Event &e = track.events[i];
        if (e.Start > ms || e.Start + e.Duration <= ms)
            continue;
        ++cost.events;
        const std::string_view text = e.Text ? std::string_view(e.Text) : std::string_view();
        cost.blurred += anyTag(text, blurs);
        cost.vectorClips += anyTag(text, clipsToADrawing);
    }
    for (const ASS_Image *img = images; img; img = img->next) {
        ++cost.images;
        cost.area += static_cast<std::int64_t>(img->w) * img->h;
    }
}

}

std::expected<OverlayFrame, RenderError> LibassRenderer::render(core::DocumentTime time, int width, int height)
{
    return draw(time, width, height, width, height);
}

std::expected<OverlayFrame, RenderError> LibassRenderer::renderScaled(core::DocumentTime time, int width, int height,
                                                                      int videoWidth, int videoHeight)
{
    return draw(time, width, height, videoWidth, videoHeight);
}

std::expected<OverlayFrame, RenderError> LibassRenderer::draw(core::DocumentTime time, int width, int height,
                                                              int videoWidth, int videoHeight)
{
    std::lock_guard lock(m_mutex);
    if (!m_engine || !m_engine->current())
        return std::unexpected(RenderError::NoSnapshot);
    const auto invalid = [](int w, int h) { return w <= 0 || h <= 0 || w > kMaxDimension || h > kMaxDimension; };
    if (invalid(width, height) || invalid(videoWidth, videoHeight))
        return std::unexpected(RenderError::InvalidSize);
    Engine &e = *m_engine;
    if (width != e.frameWidth || height != e.frameHeight) {
        ass_set_frame_size(e.renderer, width, height);
        e.frameWidth = width;
        e.frameHeight = height;
    }
    // libass lays out (and scales \blur, \be and unscaled borders) at the storage size.
    if (videoWidth != e.storageWidth || videoHeight != e.storageHeight) {
        ass_set_storage_size(e.renderer, videoWidth, videoHeight);
        e.storageWidth = videoWidth;
        e.storageHeight = videoHeight;
    }
    int change = 0;
    const long long ms = time.microseconds() / 1000; // libass times are milliseconds
    ASS_Image *images = ass_render_frame(e.renderer, e.current(), ms, &change);

    const bool resized = m_buffers.setSize(width, height);
    if (resized)
        m_bounds = {};
    OverlayFrame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.generation = m_generation;
    if (m_countCost) {
        const auto started = std::chrono::steady_clock::now();
        count(frame.cost, *e.current(), ms, images);
        frame.cost.countMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
    if (change == 0 && !m_redraw && m_buffers.shown()) {
        frame.pixels = OverlayPixels(m_buffers.shown()->bytes);
        frame.empty = emptyRect(m_bounds);
        if (!frame.empty)
            frame.bounds = m_bounds;
        return frame;
    }
    m_redraw = false;
    const std::ptrdiff_t stride = frame.stride;
    OverlayBuffers::Buffer &buffer = m_buffers.acquire();
    std::uint8_t *pixels = buffer.bytes->data();
    PixelRect bounds;
    for (ASS_Image *img = images; img; img = img->next) {
        if ((img->color & 0xff) == 0xff)
            continue; // fully transparent
        const int x0 = std::max(0, img->dst_x), y0 = std::max(0, img->dst_y);
        const int x1 = std::min(width, img->dst_x + img->w), y1 = std::min(height, img->dst_y + img->h);
        if (x0 >= x1 || y0 >= y1)
            continue;
        blendAssBitmap(pixels + y0 * stride + x0 * 4, stride,
                       img->bitmap + static_cast<std::ptrdiff_t>(y0 - img->dst_y) * img->stride + (x0 - img->dst_x),
                       img->stride, x1 - x0, y1 - y0, img->color);
        const PixelRect box{x0, y0, x1 - x0, y1 - y0};
        buffer.drawn.add(box);
        bounds = unite(bounds, box);
    }
    frame.pixels = OverlayPixels(buffer.bytes);
    frame.empty = emptyRect(bounds);
    if (!frame.empty)
        frame.bounds = bounds;
    if (resized) {
        frame.changed.push_back({0, 0, width, height});
    } else {
        if (!emptyRect(m_bounds))
            frame.changed.push_back(m_bounds);
        if (!emptyRect(bounds) && bounds != m_bounds)
            frame.changed.push_back(bounds);
    }
    m_bounds = bounds;
    return frame;
}

}
