#pragma once

// Messages of the FFMS2 media helper, shared by hikari-media-helper and the host adapter.
//
// Open    request: u8 Open, str path, i32 audioTrack (-2: every audio track; -1: none;
//                  n: track n alone), str indexFile (empty: none; an index file newer than
//                  its source is read instead of indexing, a new index is written to it),
//                  str handoffFile (empty: none; a new index that cannot be written to
//                  indexFile is written here)
//         progress: i64 done, i64 total
//         terminal Ok: i32 track, i64 fpsNum, i64 fpsDen, i64 timeBaseNum,
//                      i64 timeBaseDen, i32 frameCount, i64 pts[frameCount],
//                      i32 firstAudioTrack (-1: none), i32 audioCount,
//                      i32 audioTracks[audioCount] (every audio track, in order),
//                      i32 keyframeCount, i32 keyframes[keyframeCount] (frame
//                      indices FFMS2 marks as keyframes, ascending),
//                      u8 newIndex (1: indexed now, 0: read from indexFile),
//                      u8 handedOff (1: the new index is in handoffFile),
//                      i32 width, i32 height (frame 0's encoded size),
//                      i32 sarNum, i32 sarDen (the track's SAR; 0: unknown),
//                      i32 colorSpace, i32 colorRange (frame 0's FFMS_CS_* matrix and
//                      FFMS_CR_* range)
//         terminal failures: u8 stage, str text, as the audio box's below
// InputMatrix request: u8 InputMatrix, i32 colorSpace, i32 colorRange (FFMS_SetInputFormatV
//                  on the open video, the pixel format kept; applies to the next frames)
//         terminal Ok: empty
//         terminal InvalidInput: not open; Failed: FFMS2's text
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
// The audio box's requests. Their failures carry u8 stage (0 indexer,
// 1 indexing, 2 audio source, 3 conversion, 4 read, 5 the helper itself)
// and str text (FFMS2's error text).
// Probe   request: u8 Probe, str path (any file; independent of what is open)
//         terminal Ok: u8 hasVideo, i32 count, then per audio track i32 track,
//                      u8 hasName, str name, u8 hasLanguage, str language,
//                      str codec (the indexer's, nothing indexed)
// OpenDisplayAudio request: u8 OpenDisplayAudio, str path, i32 track,
//                  str indexFile (as Open's)
//         progress: i64 done, i64 total (indexing)
//         terminal Ok: as OpenAudio, for that track alone, decoded as S16, stereo or
//                      mono, delay FFMS_DELAY_FIRST_VIDEO_TRACK, origin 0; then u8
//                      newIndex. Replaces the open source; DisplayRead reads it.
// (Command 10 is retired.)
// DisplayRead request: u8 DisplayRead, i64 start, i64 frames, i64 decode, u8 fresh.
//                  S16 samples (not frames) [decode, frames) of the buffer are set to
//                  zero, then frames [0, decode) are decoded from start. fresh 0: the
//                  helper's block buffer, kept across reads of the open source; 1: a new
//                  zeroed buffer.
//         terminal Ok: i64 start, i64 frames, bytes the buffer's frames
//         terminal Failed, stage 4 (read): u8 stage, str text, bytes the
//                  buffer's frames as FFMS2 left them (the frames it decoded
//                  before failing, then what the buffer held)
// The next three run each request on an indexer of its own, independent of what is
// open. Strings are FFMS2's bytes up to their NUL (not decoded). A file FFMS2 cannot
// index: terminal Failed, str text (FFMS2's error text).
// SubtitleTracks request: u8 SubtitleTracks, str path
//         terminal Ok: i32 count, then per FFMS_TYPE_SUBTITLE track in order:
//                      i32 track, str name, str language, str codec
//                      (FFMS_GetSubtitleFormat; any codec, bitmaps included)
// Subtitles request: u8 Subtitles, str path, i32 track
//         progress: i64 start, i64 total (each packet; milliseconds, the total
//                   AV_NOPTS_VALUE when unknown)
//         terminal Ok: str extradata (FFMS_GetSubtitleExtradata), i32 count,
//                      then per packet in FFMS_GetSubtitles' order: i64 start,
//                      i64 duration (milliseconds), str line
//         terminal Cancelled: cancelled during or right after the read
//         terminal InvalidInput: not a subtitle track
// Attachments request: u8 Attachments, str path
//         reply, per FFMS_TYPE_ATTACHMENT track in order: i32 track,
//                      u8 hasFilename, str filename, str mimetype, bytes data
//         terminal Ok: i32 count
// Close   request: u8 Close: closes what Open, OpenAudio, PcmBegin and
//                  OpenDisplayAudio opened, so the helper holds no file; reads answer
//                  "not open"
//         terminal Ok: empty

#include <cstdint>

namespace hikari::backends::media {

inline constexpr std::uint32_t kProtocolVersion = 11;
inline constexpr char kHelperName[] = "hikari-media-helper";

enum class Command : std::uint8_t { Open = 1, Frame = 2, OpenAudio = 3, Audio = 4, Chapters = 5, PcmBegin = 6, PcmNext = 7,
                                 OpenDisplayAudio = 8, Probe = 9, DisplayRead = 11, SubtitleTracks = 12, Subtitles = 13,
                                 Attachments = 14, InputMatrix = 15, Close = 16 };

} // namespace hikari::backends::media
