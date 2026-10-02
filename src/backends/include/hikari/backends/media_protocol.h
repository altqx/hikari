#pragma once

// Messages of the isolated FFMS2 media helper (N1/N2; ADR 0016). Shared by
// hikari-media-helper and the host adapter.
//
// Open    request: u8 Open, str path
//         progress: i64 done, i64 total
//         terminal Ok: i32 track, i64 fpsNum, i64 fpsDen, i64 timeBaseNum,
//                      i64 timeBaseDen, i32 frameCount, i64 pts[frameCount]
// Frame   request: u8 Frame, i32 index
//         terminal Ok: i32 width, i32 height, i32 stride, i64 pts, bytes bgra
//         terminal InvalidInput: past the end (EOF) or not open

#include <cstdint>

namespace hikari::backends::media {

inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr char kHelperName[] = "hikari-media-helper";

enum class Command : std::uint8_t { Open = 1, Frame = 2 };

} // namespace hikari::backends::media
