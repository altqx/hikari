#pragma once

// The adapter owns and serializes the native renderer, and returns pixels it no longer writes.

#include "hikari/core/time.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application {

// Font bytes kept alive by every render context that uses them.
struct FontLease {
    std::string family;
    std::shared_ptr<const std::vector<std::byte>> bytes;
};

struct RenderSnapshot {
    std::vector<std::byte> script; // ASS bytes
    std::vector<FontLease> fonts;  // added in memory
    std::string defaultFamily;     // for unresolved names
    bool systemFonts = true;       // false: only `fonts`
};

struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const PixelRect &) const = default;
};

// Shared and immutable: copies read the same buffer and keep it alive; only an adapter writes
// one, while nothing else holds it. assign and resize make a new buffer.
class OverlayPixels {
public:
    using Bytes = std::vector<std::uint8_t>;
    OverlayPixels() = default;
    explicit OverlayPixels(std::shared_ptr<const Bytes> bytes) : m_bytes(std::move(bytes)) {}
    OverlayPixels(Bytes bytes) : m_bytes(std::make_shared<const Bytes>(std::move(bytes))) {}
    OverlayPixels(std::initializer_list<std::uint8_t> bytes) : m_bytes(std::make_shared<const Bytes>(bytes)) {}

    const std::uint8_t *data() const { return m_bytes ? m_bytes->data() : nullptr; }
    std::size_t size() const { return m_bytes ? m_bytes->size() : 0; }
    bool empty() const { return size() == 0; }
    const std::uint8_t &operator[](std::size_t i) const { return (*m_bytes)[i]; }
    const std::uint8_t *begin() const { return data(); }
    const std::uint8_t *end() const { return data() + size(); }
    // The same bytes, in this buffer or another.
    bool operator==(const OverlayPixels &other) const { return std::equal(begin(), end(), other.begin(), other.end()); }
    // The very same buffer: the adapter handed the frame out again.
    bool sameBuffer(const OverlayPixels &other) const { return m_bytes && m_bytes == other.m_bytes; }

    void assign(std::size_t count, std::uint8_t value) { m_bytes = std::make_shared<const Bytes>(count, value); }
    void resize(std::size_t count)
    {
        Bytes bytes(begin(), begin() + std::min(count, size()));
        bytes.resize(count);
        m_bytes = std::make_shared<const Bytes>(std::move(bytes));
    }

private:
    std::shared_ptr<const Bytes> m_bytes;
};

// What drawing a frame's subtitles cost; only libass counts what it drew.
struct RenderCost {
    double milliseconds = 0;      // render()'s wait, less the counting
    double countMilliseconds = 0; // the counting's share
    bool counted = false;         // the counts below are known
    int events = 0;               // events shown at the frame's time
    int images = 0;               // bitmaps the renderer produced
    std::int64_t area = 0;        // their pixels, overlaps counted again
    int blurred = 0;              // shown events with \blur or \be above 0
    int vectorClips = 0;          // shown events with a drawing \clip or \iclip
};

struct OverlayFrame {
    int width = 0, height = 0, stride = 0;
    OverlayPixels pixels; // premultiplied BGRA, top-down, `stride` bytes a row
    // Areas that differ from this renderer's previous frame: the old and the new drawn bounds (one
    // when equal, none when unchanged), so stale pixels are cleared. A new size reports the whole frame.
    std::vector<PixelRect> changed;
    // Where the renderer drew, when it knows: every pixel outside it is clear. None when nothing is
    // drawn; unset, a consumer scans the pixels (overlayBounds).
    std::optional<PixelRect> bounds;
    bool empty = true;
    std::uint64_t generation = 0; // the snapshot (context) this frame came from
    RenderCost cost;
};

enum class RenderError {
    NoSnapshot, // render before any successful prepare
    InvalidInput,
    InvalidSize, // non-positive or excessive frame size
    BackendFailure,
};

class SubtitleRendererPort {
public:
    virtual ~SubtitleRendererPort() = default;
    // Replaces the render context. Returns its generation.
    virtual std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot snapshot) = 0;
    // `time` counts to the millisecond only, as libass takes it: two times in the same millisecond
    // give the same pixels.
    virtual std::expected<OverlayFrame, RenderError> render(core::DocumentTime time, int width, int height) = 0;
    // A videoWidth x videoHeight frame laid out at the video's size, drawn at width x height; a
    // renderer that cannot draws it at the video's size.
    virtual std::expected<OverlayFrame, RenderError> renderScaled(core::DocumentTime time, int width, int height,
                                                                   int videoWidth, int videoHeight)
    {
        (void)width;
        (void)height;
        return render(time, videoWidth, videoHeight);
    }
};

struct OverlaySize {
    int width = 0, height = 0;           // the overlay's pixels
    int videoWidth = 0, videoHeight = 0; // the frame's
    bool operator==(const OverlaySize &) const = default;
    bool scaled() const { return width != videoWidth || height != videoHeight; }
};

// `screen`: none for the video's size. A screen size past the renderers' limit is scaled down to
// it, keeping its shape.
OverlaySize overlaySizeFor(int videoWidth, int videoHeight, std::optional<std::pair<int, int>> screen);

inline std::expected<OverlayFrame, RenderError> renderOverlay(SubtitleRendererPort &renderer, core::DocumentTime time,
                                                              const OverlaySize &size)
{
    if (!size.scaled())
        return renderer.render(time, size.videoWidth, size.videoHeight);
    return renderer.renderScaled(time, size.width, size.height, size.videoWidth, size.videoHeight);
}

// Counts a change of the system's fonts; a renderer that keeps its font setup across prepare()
// makes it anew at its next prepare().
void reloadSubtitleFonts();
std::uint64_t subtitleFontsReloads();

// Source-over on an opaque BGRA picture: overlay + (picture * (255 - alpha) + 127) / 255 per
// channel. An empty overlay, or one of another size, leaves the picture as it is.
void compositeOverlay(const OverlayFrame &overlay, std::uint8_t *bgra, int width, int height, std::ptrdiff_t stride);

} // namespace hikari::application
