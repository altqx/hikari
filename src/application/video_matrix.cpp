#include "hikari/application/video_matrix.h"

#include "hikari/application/edit_session.h"

namespace hikari::application {

using namespace ffms_colour;

std::string colourMatrixName(int colorSpace, int colorRange)
{
    // ProviderFFMS2::ColorMatrixDescription: "Assuming TV for unspecified".
    const std::string range = colorRange == kRangeJpeg ? "PC" : "TV";
    switch (colorSpace) {
    case kBt709: return range + ".709";
    case kFcc: return range + ".FCC";
    case kBt470bg:
    case kSmpte170m: return range + ".601";
    case kSmpte240m: return range + ".240M";
    default: return "None"; // FFMS_CS_RGB and anything else
    }
}

std::string documentVideoMatrix(bool ass, std::optional<std::string_view> scriptInfoValue, bool asLoaded)
{
    if (!ass)
        return {};
    if (asLoaded && (!scriptInfoValue || scriptInfoValue->empty() || *scriptInfoValue == "None"))
        return "TV.601";
    return std::string(scriptInfoValue.value_or(std::string_view{}));
}

std::string sessionVideoMatrix(const EditSession &session)
{
    const auto value = [](const core::Document &d) { return d.scriptInfo(u8"YCbCr Matrix"); };
    const auto current = value(session.document());
    bool asLoaded = true;
    for (std::size_t step = 0; step < session.historyCursor() && asLoaded; ++step)
        asLoaded = value(session.stepDocument(step)) == current;
    std::optional<std::string_view> text;
    if (current)
        text = std::string_view(reinterpret_cast<const char *>(current->data()), current->size());
    return documentVideoMatrix(session.document().format() == core::SubtitleFormat::Ass, text, asLoaded);
}

std::optional<LegacyColourMatrix::Input> LegacyColourMatrix::open(int colorSpace, int colorRange, int width, int height,
                                                                  std::string_view matrix)
{
    // ProviderFFMS2.cpp:368-369 and 393-414.
    m_colorSpace = colorSpace;
    m_colorRange = colorRange;
    const bool untagged = m_colorSpace == kUnspecified;
    if (untagged)
        m_colorSpace = width > 1024 || height >= 600 ? kBt709 : kBt470bg;
    m_applied = m_source = colourMatrixName(m_colorSpace, m_colorRange);
    std::optional<Input> input;
    // Approved departure V4-untagged-matrix: an untagged source is always
    // converted as its guess, the matrix it is named by. Legacy set it only
    // for a BT.709 guess with "TV.709" (ProviderFFMS2.cpp:396-412) and
    // otherwise left the converter's default BT.601 under the name TV.709, so
    // a later "TV.709" changed nothing (ProviderFFMS2.cpp:955-962).
    if (untagged || (m_colorSpace == kBt709 && matrix == "TV.709"))
        input = Input{m_colorSpace, m_colorRange};
    if (matrix == "TV.601") {
        m_applied = colourMatrixName(kBt470bg, m_colorRange);
        input = Input{kBt470bg, m_colorRange};
    } else if (matrix == "TV.709") {
        m_applied = colourMatrixName(kBt709, m_colorRange);
    }
    return input;
}

std::optional<LegacyColourMatrix::Change> LegacyColourMatrix::set(std::string_view matrix)
{
    // ProviderFFMS2.cpp:950-982.
    if (matrix == m_applied)
        return std::nullopt;
    Change change;
    change.previous = m_applied;
    if (matrix == m_source || (matrix != "TV.601" && matrix != "TV.709"))
        change.input = Input{m_colorSpace, m_colorRange};
    else if (matrix == "TV.601")
        change.input = Input{kBt470bg, m_colorRange};
    else
        return std::nullopt; // "TV.709" on a source that is not BT.709
    m_applied = std::string(matrix);
    return change;
}

} // namespace hikari::application
