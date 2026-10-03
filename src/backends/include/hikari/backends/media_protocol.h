#pragma once

// Messages of the isolated FFMS2 media helper (N1/N2; ADR 0016). Shared by
// hikari-media-helper and the host adapter.
//
// Open    request: u8 Open, str path
//         progress: i64 done, i64 total
//         terminal Ok: i32 track, i64 fpsNum, i64 fpsDen, i64 timeBaseNum,
//                      i64 timeBaseDen, i32 frameCount, i64 pts[frameCount],
//                      i32 firstAudioTrack (-1: none), i32 audioCount,
//                      i32 audioTracks[audioCount] (every audio track, in order),
//                      i32 keyframeCount, i32 keyframes[keyframeCount] (frame
//                      indices FFMS2 marks as keyframes, ascending)
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
// PcmBegin request: u8 PcmBegin, i64 start, i64 count, i32 outRate, i32 outChannels
//         terminal Ok: i64 start, i64 count (shortened at the end), i64 total
//                      output frames (the range's duration at outRate, rounded)
//         A resampled stream over the open audio track; replaces any earlier one.
// PcmNext request: u8 PcmNext, i64 maxFrames
//         terminal Ok: i64 frames, u8 end, bytes interleaved float32 at the
//                      output format; chunks continue one resampler
// Chapters request: u8 Chapters, str path (any file; independent of Open)
//         terminal Ok: i32 count, then per chapter i64 startUs, i64 endUs,
//                      str title

#include <cstdint>

namespace hikari::backends::media {

inline constexpr std::uint32_t kProtocolVersion = 3; // 2: audio tracks; 3: keyframes in the Open reply
inline constexpr char kHelperName[] = "hikari-media-helper";

enum class Command : std::uint8_t { Open = 1, Frame = 2, OpenAudio = 3, Audio = 4, Chapters = 5, PcmBegin = 6, PcmNext = 7 };

} // namespace hikari::backends::media
