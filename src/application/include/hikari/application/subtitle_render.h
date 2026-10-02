#pragma once

// SubtitleRenderer port (N3; docs/qt/backends.md). Value records only: the
// application prepares an explicit snapshot (script bytes, font-byte leases,
// configuration) and asks for an overlay at a time and size. The adapter owns
// the native renderer, serializes it, and returns owned pixels.

#include "hikari/core/time.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
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

// Premultiplied BGRA, top-down, `stride` bytes per row.
struct OverlayFrame {
    int width = 0, height = 0, stride = 0;
    std::vector<std::uint8_t> pixels;
    // Areas that differ from the previous frame of the same context, in pixel
    // coordinates: the old and new bounds of what changed. A disappearing
    // overlay reports its old bounds, so stale pixels are always cleared.
    std::vector<PixelRect> changed;
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
    virtual std::expected<OverlayFrame, RenderError> render(core::DocumentTime time, int width, int height) = 0;
};

} // namespace hikari::application
