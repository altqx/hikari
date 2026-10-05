#pragma once

// Messages of the isolated FFMS2 media helper (N1/N2; ADR 0016). Shared by
// hikari-media-helper and the host adapter.
//
// Open    request: u8 Open, str path, i32 audioTrack (-2: every audio track;
//                  -1: none; n: track n alone, as legacy ProviderFFMS2::Init
//                  indexed the chosen track), str indexFile (legacy's index
//                  cache, Indices/<name>_<track>.ffindex; empty: none). An
//                  index file newer than the source that belongs to it is read
//                  instead of indexing; a new index is written to it.
//                  str handoffFile (A1; empty: none): when a new index cannot
//                  be written to indexFile, it is written here instead, so the
//                  audio box's helper reads it rather than indexing again
//                  (legacy's box shared the video's index in memory).
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
//                      i32 sarNum, i32 sarDen (the track's SAR; 0: unknown)
//         terminal failures (8): u8 stage, str text, as the audio box's below
//                      (V3: legacy ProviderFFMS2::Init's log messages)
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
// The audio box's requests (A1). Their failures carry u8 stage (0 indexer,
// 1 indexing, 2 audio source, 3 conversion, 4 read, 5 the helper itself)
// and str text (FFMS2's error text).
// Probe   request: u8 Probe, str path (any file; independent of what is open)
//         terminal Ok: u8 hasVideo, i32 count, then per audio track i32 track,
//                      u8 hasName, str name, u8 hasLanguage, str language,
//                      str codec (the indexer's, nothing indexed)
// OpenDisplayAudio request: u8 OpenDisplayAudio, str path, i32 track,
//                  str indexFile (as Open's)
//         progress: i64 done, i64 total (indexing)
//         terminal Ok: as OpenAudio, for that track alone indexed, in
//                      legacy's decode format (S16; stereo or mono; delay
//                      FFMS_DELAY_FIRST_VIDEO_TRACK; origin 0), then u8
//                      newIndex. Replaces the open source; DisplayRead reads it.
// (10, OpenSourceDisplayAudio, read the open video's audio in the video's own
// helper in protocol 5; removed in 6: the box has a helper of its own.)
// DisplayRead request: u8 DisplayRead, i64 start, i64 frames, i64 decode,
//                  u8 fresh. Legacy ProviderFFMS2::GetAudio into a cache's
//                  buffer: S16 samples [decode, frames) of the buffer are set
//                  to zero (legacy's "fill beyond" counts samples, not
//                  frames), then frames [0, decode) are decoded from start.
//                  fresh 0: the helper's block buffer, kept from read to read
//                  of the open source (legacy DiskCache's reused `data`);
//                  1: a new zeroed buffer (legacy RAMCache's new block).
//         terminal Ok: i64 start, i64 frames, bytes the buffer's frames
//         terminal Failed, stage 4 (read): u8 stage, str text, bytes the
//                  buffer's frames as FFMS2 left them (the frames it decoded
//                  before failing, then what the buffer held)
// Y9: legacy Demux (Demux.cpp at 20d647c4), each request on an indexer of
// its own, independent of what is open. Strings are FFMS2's bytes up to their
// NUL (not decoded). A file FFMS2 cannot index: terminal Failed, str text
// (FFMS2's error text).
// SubtitleTracks request: u8 SubtitleTracks, str path
//         terminal Ok: i32 count, then per FFMS_TYPE_SUBTITLE track in order:
//                      i32 track, str name, str language, str codec
//                      (FFMS_GetSubtitleFormat; any codec, bitmaps included)
// Subtitles request: u8 Subtitles, str path, i32 track
//         progress: i64 start, i64 total (each packet, as legacy's callback)
//         terminal Ok: str extradata (FFMS_GetSubtitleExtradata), i32 count,
//                      then per packet in FFMS_GetSubtitles' order: i64 start,
//                      i64 duration, str line
//         terminal Cancelled: cancelled during or right after the read
//         terminal InvalidInput: not a subtitle track
// Attachments request: u8 Attachments, str path
//         reply, per FFMS_TYPE_ATTACHMENT track in order: i32 track,
//                      u8 hasFilename, str filename, str mimetype, bytes data
//         terminal Ok: i32 count

#include <cstdint>

namespace hikari::backends::media {

inline constexpr std::uint32_t kProtocolVersion = 9; // 2: audio tracks; 3: keyframes in the Open reply; 4: OpenDisplayAudio;
                                                   // 5: Probe, a chosen track, the video's audio, DisplayRead;
                                                   // 6: index files and the audio track in Open, no shared display audio;
                                                   // 7: Open's index handoff, DisplayRead into a block buffer;
                                                   // 8: SubtitleTracks, Subtitles, Attachments (Y9);
                                                   // 9: Open's failures carry their stage (V3)
inline constexpr char kHelperName[] = "hikari-media-helper";

enum class Command : std::uint8_t { Open = 1, Frame = 2, OpenAudio = 3, Audio = 4, Chapters = 5, PcmBegin = 6, PcmNext = 7,
                                 OpenDisplayAudio = 8, Probe = 9, DisplayRead = 11, SubtitleTracks = 12, Subtitles = 13,
                                 Attachments = 14 };

} // namespace hikari::backends::media
