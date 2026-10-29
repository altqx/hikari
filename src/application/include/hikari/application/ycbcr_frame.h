#pragma once

#include "hikari/application/indexed_source.h"

#include <cstddef>
#include <string>

namespace hikari::application {

// Strides are 4-byte aligned.
YcbcrPlanes ycbcrLayout(int width, int height, bool interleaved, bool fullHeightChroma,
                        const yuv::Matrix &matrix);
std::size_t ycbcrBytes(const YcbcrPlanes &planes);

// Empty when the frame's planes fit its buffer and its size.
std::string ycbcrLayoutError(const IndexedFrame &frame);

// Opaque BGRA, stride width * 4. False when the layout is invalid.
bool ycbcrToBgra(const IndexedFrame &frame, IndexedFrame &out);

}
