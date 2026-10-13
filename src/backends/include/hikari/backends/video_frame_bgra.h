#pragma once

// V1: a general player frame (QVideoFrame) as opaque BGRA in an
// IndexedFrame, written into the frame's own buffer (a pooled one is reused
// as it is). The decoder's 8- and 16-bit 4:2:0 and 4:2:2 Y'CbCr layouts
// (NV12, NV21, P010, P016, YUV420P, YV12, YUV420P10, YUV422P) are converted
// here with the frame's matrix and range (BT.601, BT.709 or BT.2020; an
// unspecified matrix is BT.601 up to 576 lines and BT.709 above); 16-bit
// samples are rounded to 8 bits first. Other formats go through
// QVideoFrame::toImage. Safe on any thread.

#include "hikari/application/indexed_source.h"

class QVideoFrame;

namespace hikari::backends {

// False when the frame is invalid, cannot be mapped or has no image.
bool videoFrameToBgra(const QVideoFrame &frame, application::IndexedFrame &out);

} // namespace hikari::backends
