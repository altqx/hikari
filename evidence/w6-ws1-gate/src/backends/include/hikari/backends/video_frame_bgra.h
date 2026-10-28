#pragma once

// V1: a general player frame (QVideoFrame) as opaque BGRA in an
// IndexedFrame, written into the frame's own buffer (a pooled one is reused
// as it is). The decoder's 8- and 16-bit 4:2:0 and 4:2:2 Y'CbCr layouts
// (NV12, NV21, P010, P016, YUV420P, YV12, YUV420P10, YUV422P) are converted
// here; 16-bit samples are rounded to 8 bits first. Other formats go
// through QVideoFrame::toImage. Safe on any thread (a hardware frame is
// read back by QVideoFrame::map, on the calling thread).
//
// The matrix and range are `colour`'s when given: the input matrix the
// indexed video is converted with (V4's legacy YCbCr matrix, FFMS2's
// constants), so a player frame has the colours of the paused frame at the
// same time, as legacy's playback (the same FFMS2 provider) had. Without
// it, the frame's own, an untagged one guessed as legacy did (BT.709 wider
// than 1024 or 600 lines and up, else BT.601). Like the indexed frames
// (FFMS2 through swscale) there is no transfer conversion: BT.2020 PQ or HLG
// is shown without tone mapping. Rotation and mirroring (the stream's or the
// frame's) are not applied: the indexed frames, the subtitles' frame size
// and legacy all take the coded picture as it is, and Qt's toImage ignores
// the frame's own too.

#include "hikari/application/indexed_source.h"
#include "hikari/application/video_matrix.h"

#include <optional>

class QVideoFrame;

namespace hikari::backends {

// False when the frame is invalid, cannot be mapped or has no image.
bool videoFrameToBgra(const QVideoFrame &frame, application::IndexedFrame &out,
                      std::optional<application::LegacyColourMatrix::Input> colour = std::nullopt);

// The same frame for a presenter that converts Y'CbCr on the GPU: the layouts
// above are copied into the frame's planes (IndexedFrame::ycbcr, 16-bit
// samples rounded to 8 bits as above) with the matrix videoFrameToBgra would
// use, so that application::ycbcrToBgra of the result is videoFrameToBgra's
// output byte for byte. Other formats are made BGRA as above.
bool videoFrameToYcbcr(const QVideoFrame &frame, application::IndexedFrame &out,
                       std::optional<application::LegacyColourMatrix::Input> colour = std::nullopt);

} // namespace hikari::backends
