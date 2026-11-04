#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/resample.h"

#include <expected>
#include <string>
#include <string_view>

namespace hikari::application {

inline constexpr std::string_view kMatrixStep = "Setting the YCbCr Matrix";
inline constexpr std::string_view kLayoutResolutionStep = "Setting the layout resolution";

// The video.missingMatrix, video.matrixMismatch and video.missingLayoutRes values.
enum class VideoPropertyAction { Ignore = 0, Ask = 1, Set = 2, SetToPlayRes = 3 };
// The video.layoutResMismatch values.
enum class LayoutResMismatch { Never = 0, OnAspectChange = 1, Always = 2 };

// Frame 0's FFMS_CS_* matrix, FFMS_CR_* range, primaries and transfer, as the media helper reports them.
struct VideoColour {
    int colorSpace = 2, colorRange = 0;
    int primaries = 2, transfer = 2;
};

// Aegisub 3.5's ycbcr guess_colorspace then to_best_practice: an untagged matrix is BT.709 when wider than
// 1024 or at least 600 high, else BT.601, and an untagged range is TV; "TV.601", "TV.709", "PC.601" or
// "PC.709", else "None".
std::string recommendedMatrix(const VideoColour &colour, int width, int height);
bool untaggedMatrix(const VideoColour &colour);
// PQ or HLG transfer, or BT.2020, DCI-P3 or Display P3 primaries.
bool hdrOrWideGamut(const VideoColour &colour);
// The aspect ratios differ by more than 1%.
bool aspectRatioChanged(Resolution script, Resolution video);

enum class MatrixQuestion { None, Missing, Mismatch };
struct MatrixCheck {
    MatrixQuestion question = MatrixQuestion::None;
    std::string script;      // the written value
    std::string recommended; // recommendedMatrix
};
// Missing: no YCbCr Matrix key, or one that names no matrix. Mismatch: a matrix other than None that differs
// from `recommended`. None for non-ASS.
MatrixCheck checkMatrix(const core::Document &document, std::string_view recommended);

enum class LayoutQuestion { None, Missing, Mismatch };
struct LayoutCheck {
    LayoutQuestion question = LayoutQuestion::None;
    Resolution layout;       // the written LayoutResX/Y
    Resolution play;         // the written PlayResX/Y
    bool aspectChanged = false;
};
// Missing: either LayoutRes key below 1. Mismatch: a LayoutRes other than the video's, asked as `condition`
// says. None for non-ASS.
LayoutCheck checkLayoutResolution(const core::Document &document, Resolution video, LayoutResMismatch condition);

// One undo step each; refused for non-ASS; no step when nothing changes.
std::expected<void, CommandRefusal> setMatrix(EditSession &session, std::string_view matrix);
std::expected<void, CommandRefusal> setLayoutResolution(EditSession &session, Resolution layout);

}
