#pragma once

// Messages of the isolated FFMS2 media helper (N1/N2; ADR 0016). Shared by
// hikari-media-helper and the host adapter.
//
// Open    request: u8 Open, str path
//         progress: i64 done, i64 total
//         terminal Ok: i32 track, i64 fpsNum, i64 fpsDen, i64 timeBaseNum,
//                      i64 timeBaseDen, i32 frameCount, i64 pts[frameCount],
//                      i32 firstAudioTrack (-1: none)
// Frame   request: u8 Frame, i32 index
//         terminal Ok: i32 width, i32 height, i32 stride, i64 pts, bytes bgra
//         terminal InvalidInput: past the end (EOF) or not open
// OpenAudio request: u8 OpenAudio, i32 track
//         terminal Ok: i32 track, i32 sampleFormat (FFMS_FMT_*), i32 rate,
//                      i32 bitsPerSample, i32 channels, i64 channelLayout,
//                      i64 sampleCount, i64 originMicroseconds
// Audio   request: u8 Audio, i64 start, i64 count (a half-open range)
//         terminal Ok: i64 start, i64 count (shortened at the end), bytes
//                      interleaved samples
//         terminal InvalidInput: start past the end (EOF) or audio not open

#include <cstdint>

namespace hikari::backends::media {

inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr char kHelperName[] = "hikari-media-helper";

enum class Command : std::uint8_t { Open = 1, Frame = 2, OpenAudio = 3, Audio = 4 };

} // namespace hikari::backends::media
