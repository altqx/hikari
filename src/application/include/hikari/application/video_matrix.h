#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

class EditSession;

// FFMS2's FFMS_ColorSpaces / FFMS_ColorRanges values (libavutil's AVCOL_SPC_* / AVCOL_RANGE_*).
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

// "TV." or "PC." (full range) with 601, 709, FCC or 240M, else "None".
std::string colourMatrixName(int colorSpace, int colorRange);

// Empty for non-ASS. A missing or "None" value `asLoaded` reads "TV.601", as HikariSub legacy wrote it on
// load; a "None" the user chose stays "None".
std::string documentVideoMatrix(bool ass, std::optional<std::string_view> scriptInfoValue, bool asLoaded);

// The value counts as loaded while no kept history step up to the current one changed it.
std::string sessionVideoMatrix(const EditSession &session);

class LegacyColourMatrix {
public:
    // FFMS_SetInputFormatV's colour space and range.
    struct Input {
        int colorSpace = ffms_colour::kUnspecified;
        int colorRange = ffms_colour::kRangeUnspecified;
        bool operator==(const Input &) const = default;
    };
    // `previous`: the name to restore if the converter refuses the change.
    struct Change {
        Input input;
        std::string previous;
    };

    // From the first frame's matrix, range and encoded size. An untagged source is BT.709 when wider than
    // 1024 or at least 600 high, else BT.601. nullopt leaves the converter's input alone.
    std::optional<Input> open(int colorSpace, int colorRange, int width, int height, std::string_view matrix);
    // nullopt leaves the converter's input alone.
    std::optional<Change> set(std::string_view matrix);
    void revert(const Change &change) { m_applied = change.previous; }

    // The name of the matrix the video is converted with.
    const std::string &applied() const { return m_applied; }
    // The source's own (guessed when untagged).
    const std::string &source() const { return m_source; }
    int sourceColorSpace() const { return m_colorSpace; }
    int sourceColorRange() const { return m_colorRange; }

private:
    int m_colorSpace = ffms_colour::kUnspecified;
    int m_colorRange = ffms_colour::kRangeUnspecified;
    std::string m_applied, m_source;
};

} // namespace hikari::application
