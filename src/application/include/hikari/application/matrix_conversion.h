#pragma once

#include "hikari/core/document.h"
#include "hikari/core/style.h"

#include <array>
#include <optional>
#include <string_view>

namespace hikari::application {

// Keeps a colour's look on the video when the YCbCr Matrix it is rendered under changes: RGB to YCbCr by the
// old matrix, then back to RGB by the new one, as Aegisub's ycbcr_converter does.
class MatrixConversion {
public:
    // Both must name a matrix ("TV.601", "PC.709", "TV.FCC", "PC.240M", …; not "None"), and differ.
    static std::optional<MatrixConversion> between(std::string_view from, std::string_view to);

    // Alpha is kept.
    core::Colour convert(core::Colour colour) const;

private:
    std::array<double, 9> m_toYcbcr{}, m_fromYcbcr{};
    std::array<double, 3> m_shiftTo{}, m_shiftFrom{};
};

// "TV." or "PC." with 601, 709, FCC or 240M: a matrix MatrixConversion converts from and to.
bool namesConvertibleMatrix(std::string_view name);

// Every Style colour, and each \c and \1c–\4c (inside \t too) in every Line's text and translation, except
// karaoke template and code comments.
void convertColours(core::Document &document, const MatrixConversion &conversion);

}
