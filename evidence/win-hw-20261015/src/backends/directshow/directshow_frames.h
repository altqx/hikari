#pragma once

// W1: a DirectShow video sample as a BGRA frame. Legacy uploaded the sample
// to a DXVA surface (RendererDirectShow::DrawTexture) and let
// VideoProcessBlt convert it with limited-range input (16-235), the script's
// matrix and full-range output; this does the same conversion on the CPU.
// The sample layout is legacy's: the negotiated width (made even) times its
// bytes per pixel, the planes one after another (YV12: Y, V, U; NV12: Y,
// then interleaved U and V).

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

// The bytes a sample of this format holds (legacy m_Height * m_Pitch).
std::size_t sampleBytes(const SampleFormat &format);
// False when the sample is shorter than its format says.
bool toBgra(const SampleFormat &format, const std::uint8_t *data, std::size_t size, bool bt709,
            application::IndexedFrame &out);
const char *subtypeName(Subtype subtype);

} // namespace hikari::backends::dshow
