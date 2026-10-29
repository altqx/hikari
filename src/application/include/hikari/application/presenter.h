#pragma once

// Submitting never waits for decoding or rendering; each submission resolves once.

#include "hikari/application/indexed_source.h"
#include "hikari/application/subtitle_render.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace hikari::application {

struct PresentationTransform {
    // Sample (pixel) aspect ratio of the frame. The frame is fitted into the
    // presenter, centred, at its display aspect; the overlay covers the frame.
    double pixelAspect = 1.0;
};

struct Presentation {
    std::uint64_t generation = 0;               // the source generation of the frame
    std::shared_ptr<const IndexedFrame> frame;  // opaque BGRA, or Y'CbCr planes (IndexedFrame::ycbcr)
    std::shared_ptr<const OverlayFrame> overlay; // premultiplied BGRA at the frame's size; may be null
    PresentationTransform transform;
    // With overlayBoundsKnown, where the overlay's visible pixels are (nullopt:
    // none); otherwise the presenter finds them itself.
    bool overlayBoundsKnown = false;
    std::optional<PixelRect> overlayBounds;
};

enum class PresentOutcome {
    Accepted,   // shown
    Superseded, // a newer submission replaced it first
    Error,      // invalid input, a failed upload, or the surface changed first
};
enum class PresentStage { Submitted, Uploaded, Rendered };

struct PresentResult {
    std::uint64_t generation = 0;
    PresentOutcome outcome = PresentOutcome::Error;
    PresentStage stage = PresentStage::Submitted; // the last stage it reached
    std::string message;
};

class PresenterPort {
public:
    using Done = std::function<void(PresentResult)>;
    virtual ~PresenterPort() = default;
    virtual void present(Presentation presentation, Done done) = 0;
    // Whether Y'CbCr-plane frames are converted on the GPU (else on the CPU). Any
    // thread.
    virtual bool convertsYcbcr() const { return false; }
};

}
