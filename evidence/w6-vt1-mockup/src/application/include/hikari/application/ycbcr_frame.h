#pragma once

// V1: player frames kept as Y'CbCr planes (IndexedFrame::ycbcr) for the
// presenter to convert on the GPU, as the shader in src/ui/shaders/ycbcr.frag
// does, with the fixed-point formula of yuv_bgra.h. The players' adapters
// copy their samples into these planes (16-bit samples rounded to 8 bits,
// the chroma rows a short mapping lacks repeated from its last one) so that
// the planes give exactly the pixels the CPU conversion gives; ycbcrToBgra is
// that conversion, for a presenter without the GPU path.

#include "hikari/application/indexed_source.h"

#include <cstddef>
#include <string>

namespace hikari::application {

// The planes of a width x height frame (offsets and 4-byte aligned strides)
// and the bytes they take.
YcbcrPlanes ycbcrLayout(int width, int height, bool interleaved, bool fullHeightChroma,
                        const yuv::Matrix &matrix);
std::size_t ycbcrBytes(const YcbcrPlanes &planes);

// Empty when the frame's planes fit its buffer and its size.
std::string ycbcrLayoutError(const IndexedFrame &frame);

// The frame's BGRA (opaque, stride width * 4), as the CPU path would have
// made it from the same samples. False when the layout is invalid.
bool ycbcrToBgra(const IndexedFrame &frame, IndexedFrame &out);

} // namespace hikari::application
