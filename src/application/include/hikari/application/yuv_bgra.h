#pragma once

// The SSE2 and scalar paths give the same bytes. Pixel x takes u[x / 2] and
// v[x / 2], no interpolation.

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
