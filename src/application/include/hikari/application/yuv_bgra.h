#pragma once

// Y'CbCr rows to opaque BGRA for the playback paths that convert on the CPU
// (V1's player frames, W1's DirectShow samples). One fixed-point formula,
// with SSE2 where the target has it and the same integer arithmetic
// elsewhere, so both give the same bytes:
//
//   c = cy * (Y - yOffset)
//   B = clamp((c + bu * (U - 128) + round) >> shift)
//   G = clamp((c + gu * (U - 128) + gv * (V - 128) + round) >> shift)
//   R = clamp((c + rv * (V - 128) + round) >> shift)
//
// with round = 1 << (shift - 1). Chroma is at half the horizontal
// resolution: pixel x takes u[x / 2] and v[x / 2] (no interpolation, as
// legacy's DirectShow conversion and Qt's CPU conversion did).

#include <cstdint>

namespace hikari::application::yuv {

struct Matrix {
    // Coefficients and offsets must fit in 16 bits (shift 13 or less does).
    int cy = 0, rv = 0, gu = 0, gv = 0, bu = 0;
    int yOffset = 16; // 16 for limited range, 0 for full range
    int shift = 13;
};

// The luma coefficients of BT.601 (and SMPTE 170M, BT.470BG), BT.709,
// BT.2020 (non-constant luminance), FCC and SMPTE 240M.
enum class Space { Bt601, Bt709, Bt2020, Fcc, Smpte240m };

// The matrix with 13 fractional bits (limited range: luma 16-235, chroma 16-240).
Matrix matrix(Space space, bool fullRange);

// `width` pixels of one row. `u` and `v` hold (width + 1) / 2 samples.
void row(const Matrix &m, const std::uint8_t *y, const std::uint8_t *u, const std::uint8_t *v, int width,
         std::uint8_t *bgra);

} // namespace hikari::application::yuv
