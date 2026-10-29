#pragma once

#include "hikari/application/indexed_source.h"
#include "hikari/application/video_matrix.h"

#include <optional>

class QVideoFrame;

namespace hikari::backends {

// Safe on any thread. Without `colour`, the frame's own matrix (untagged: BT.709 when wider than
// 1024 or 600+ lines high, else BT.601). No tone mapping, rotation or mirroring is applied.
// False when the frame is invalid, cannot be mapped or has no image.
bool videoFrameToBgra(const QVideoFrame &frame, application::IndexedFrame &out,
                      std::optional<application::LegacyColourMatrix::Input> colour = std::nullopt);

// Fills IndexedFrame::ycbcr so that application::ycbcrToBgra gives videoFrameToBgra's bytes;
// formats without a Y'CbCr layout come out as BGRA.
bool videoFrameToYcbcr(const QVideoFrame &frame, application::IndexedFrame &out,
                       std::optional<application::LegacyColourMatrix::Input> colour = std::nullopt);

} // namespace hikari::backends
