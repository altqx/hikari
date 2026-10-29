#pragma once

// A DirectShow sample: the width made even, the planes back to back (YV12: Y, V, U;
// NV12: Y, then interleaved U and V); Y'CbCr goes from limited range to full-range BGRA.

#include "hikari/application/indexed_source.h"

#include <cstddef>
#include <cstdint>

namespace hikari::backends::dshow {

enum class Subtype { RGB32, YUY2, YV12, NV12 };

struct SampleFormat {
    Subtype subtype = Subtype::RGB32;
    int width = 0;  // the negotiated biWidth, made even as legacy did
    int height = 0; // rows (positive)
    bool bottomUp = false; // an RGB DIB with a positive biHeight
};

std::size_t sampleBytes(const SampleFormat &format);
// False when the sample is shorter than its format says.
bool toBgra(const SampleFormat &format, const std::uint8_t *data, std::size_t size, bool bt709,
            application::IndexedFrame &out);
// YV12 and NV12 are copied into IndexedFrame::ycbcr so that application::ycbcrToBgra
// matches toBgra byte for byte; RGB32 and YUY2 go through toBgra.
bool toYcbcr(const SampleFormat &format, const std::uint8_t *data, std::size_t size, bool bt709,
             application::IndexedFrame &out);
const char *subtypeName(Subtype subtype);

} // namespace hikari::backends::dshow
