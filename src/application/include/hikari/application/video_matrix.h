#pragma once

// V4: the Script properties YCbCr matrix applied to the indexed video's
// colours, as legacy ProviderFFMS2 did at 20d647c4. The media helper converts
// Y'CbCr to BGRA with FFMS2 (I2); legacy overrode the converter's input
// matrix with FFMS_SetInputFormatV, and this records when and with what:
//
//   - opening (ProviderFFMS2::Init, ProviderFFMS2.cpp:368-414): the frame's
//     matrix and range; an unspecified matrix is taken as BT.709 for a frame
//     wider than 1024 or at least 600 high, else BT.601 (BT470BG). With the
//     Document's "TV.709" a BT.709 source is set to BT.709 (an untagged one
//     is otherwise converted with the converter's default); with "TV.601"
//     every source is converted as BT.601; anything else leaves the source's
//     own tags;
//   - a changed matrix (ProviderFFMS2::SetColorSpace, ProviderFFMS2.cpp:950-982):
//     nothing when it names what is applied; the source's own matrix (the
//     guess above for an untagged one) for the source's own name or any name
//     but "TV.601" and "TV.709"; BT.601 for "TV.601"; nothing for "TV.709"
//     on a source that is not BT.709.
//
// The range is always the source's. Names follow ColorMatrixDescription
// (ProviderFFMS2.cpp:929-948): "TV." or "PC." (a full-range source) with
// 601, 709, FCC or 240M, else "None". Legacy's answer depends on the order
// of the changes, so this keeps legacy's state rather than a function of
// the current matrix alone.

#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

class EditSession;

// FFMS2's colour constants (ffms.h FFMS_ColorSpaces / FFMS_ColorRanges,
// libavutil's AVCOL_SPC_* / AVCOL_RANGE_* values).
namespace ffms_colour {
inline constexpr int kRgb = 0;
inline constexpr int kBt709 = 1;
inline constexpr int kUnspecified = 2;
inline constexpr int kFcc = 4;
inline constexpr int kBt470bg = 5;
inline constexpr int kSmpte170m = 6;
inline constexpr int kSmpte240m = 7;
inline constexpr int kRangeUnspecified = 0;
inline constexpr int kRangeMpeg = 1;
inline constexpr int kRangeJpeg = 2;
} // namespace ffms_colour

// ColorMatrixDescription.
std::string colourMatrixName(int colorSpace, int colorRange);

// The matrix legacy's video saw for a Document: an ASS Document's Script
// Info "YCbCr Matrix", and no matrix for any other format. A missing or
// "None" value the file was loaded with reads "TV.601": legacy
// SubsLoader::LoadASS wrote that into every loaded ASS file
// (SubsLoader.cpp:167-168) before the video saw it, and approved departure
// C03-ycbcr-on-load keeps the file as loaded, so the video takes legacy's
// value here instead. A "None" the user chose in Script properties stays
// "None": legacy wrote it and called SetColorSpace("None")
// (HikariSubFrame.cpp:1241-1245), which gives the source's own matrix.
std::string documentVideoMatrix(bool ass, std::optional<std::string_view> scriptInfoValue, bool asLoaded);

// documentVideoMatrix for the session's Document, where the value is as
// loaded while no kept history step up to the current one changed it (Undo
// back to the opened value gives legacy's TV.601 again, as legacy's Undo
// restored the TV.601 its load wrote, SubsGridBase.cpp:1023-1025). Steps
// dropped past the 500-step history capacity are not seen.
std::string sessionVideoMatrix(const EditSession &session);

class LegacyColourMatrix {
public:
    // FFMS_SetInputFormatV's colour space and range.
    struct Input {
        int colorSpace = ffms_colour::kUnspecified;
        int colorRange = ffms_colour::kRangeUnspecified;
        bool operator==(const Input &) const = default;
    };
    // A change asked of the converter, and the name to restore if it fails
    // (legacy keeps its old name then, ProviderFFMS2.cpp:973-978).
    struct Change {
        Input input;
        std::string previous;
    };

    // ProviderFFMS2::Init: the source's first frame matrix, range and
    // encoded size, and the Document's matrix.
    std::optional<Input> open(int colorSpace, int colorRange, int width, int height, std::string_view matrix);
    // ProviderFFMS2::SetColorSpace.
    std::optional<Change> set(std::string_view matrix);
    void revert(const Change &change) { m_applied = change.previous; }

    // m_colorSpace: the matrix the video is converted with by name.
    const std::string &applied() const { return m_applied; }
    // m_realColorSpace: the source's own (guessed when untagged).
    const std::string &source() const { return m_source; }
    int sourceColorSpace() const { return m_colorSpace; }
    int sourceColorRange() const { return m_colorRange; }

private:
    int m_colorSpace = ffms_colour::kUnspecified;
    int m_colorRange = ffms_colour::kRangeUnspecified;
    std::string m_applied, m_source;
};

} // namespace hikari::application
