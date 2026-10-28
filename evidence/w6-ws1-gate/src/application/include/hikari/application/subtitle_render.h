#pragma once

// SubtitleRenderer port (N3; docs/qt/backends.md). Value records only: the
// application prepares an explicit snapshot (script bytes, font-byte leases,
// configuration) and asks for an overlay at a time and size. The adapter owns
// the native renderer, serializes it, and returns pixels it no longer writes.

#include "hikari/core/time.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

// Font bytes kept alive by every render context that uses them.
struct FontLease {
    std::string family;
    std::shared_ptr<const std::vector<std::byte>> bytes;
};

struct RenderSnapshot {
    std::vector<std::byte> script; // ASS script bytes (a Document's encoded form)
    std::vector<FontLease> fonts;  // attachments or collected fonts, added in memory
    std::string defaultFamily;     // fallback family for unresolved names
    bool systemFonts = true;       // false: only `fonts` (deterministic tests, collected sets)
};

struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const PixelRect &) const = default;
};

// An overlay's pixel bytes, shared and immutable: every copy reads the same
// buffer, so an adapter can hand an unchanged frame out again, and every
// holder of a frame keeps its pixels alive. Only an adapter writes a buffer,
// and only while nothing else holds it. assign and resize make a new buffer.
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

// Premultiplied BGRA, top-down, `stride` bytes per row.
struct OverlayFrame {
    int width = 0, height = 0, stride = 0;
    OverlayPixels pixels;
    // Areas that differ from the previous frame of the same renderer, in pixel
    // coordinates, inside the frame: the old and the new bounds of what is
    // drawn (one rectangle when they are the same, none when nothing changed).
    // A disappearing overlay reports its old bounds, so stale pixels are
    // always cleared. A new size reports the whole frame.
    std::vector<PixelRect> changed;
    // Where the renderer drew, when it knows: every pixel outside it is
    // clear (it may hold clear pixels too). None when nothing is drawn.
    // Unset, a consumer scans the pixels (overlayBounds).
    std::optional<PixelRect> bounds;
    bool empty = true;
    std::uint64_t generation = 0; // the snapshot (context) this frame came from
};

enum class RenderError {
    NoSnapshot,     // render before any successful prepare
    InvalidInput,   // the script could not be read
    InvalidSize,    // non-positive or excessive frame size
    BackendFailure, // the native renderer failed
};

class SubtitleRendererPort {
public:
    virtual ~SubtitleRendererPort() = default;
    // Replaces the render context. Returns its generation.
    virtual std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot snapshot) = 0;
    // The overlay at `time`, which counts to the millisecond only
    // (time.microseconds() / 1000, as libass's and CSRI's draw calls take
    // it): two times in the same millisecond give the same pixels, so the
    // V1 playback pipeline may use an overlay rendered ahead for any frame
    // starting in that millisecond.
    virtual std::expected<OverlayFrame, RenderError> render(core::DocumentTime time, int width, int height) = 0;
};

// Legacy Notebook::RefreshVideo(true) (FontEnumerator, when the fonts on the
// system change) made libass's library and renderer, which every tab and
// preview shares, anew (SubtitlesProviderManager::ReloadLibraries).
// reloadSubtitleFonts counts such a change; a renderer that keeps its font
// setup across prepare() makes it anew at its first prepare() after one.
void reloadSubtitleFonts();
std::uint64_t subtitleFontsReloads();

// The overlay laid source-over on an opaque BGRA picture (`stride` bytes a
// row): each colour channel overlay + (picture * (255 - alpha) + 127) / 255,
// the divisions rounded, as snapshotImage lays it. An empty overlay, or one
// of another size, leaves the picture as it is.
void compositeOverlay(const OverlayFrame &overlay, std::uint8_t *bgra, int width, int height, std::ptrdiff_t stride);

} // namespace hikari::application
