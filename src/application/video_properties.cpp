#include "hikari/application/video_properties.h"

#include "hikari/application/video_matrix.h"

#include <array>
#include <cmath>
#include <cstdlib>

namespace hikari::application {

namespace {

constexpr std::array kWideGamutPrimaries = {ffms_colour::kPrimariesBt2020, ffms_colour::kPrimariesDciP3,
                                           ffms_colour::kPrimariesDisplayP3};

std::string text(const std::optional<std::u8string_view> &value)
{
    return value ? std::string(value->begin(), value->end()) : std::string();
}

int intOf(const core::Document &document, std::u8string_view key)
{
    return std::atoi(text(document.scriptInfo(key)).c_str());
}

bool namesMatrix(std::string_view value)
{
    if (value == "None")
        return true;
    if (!value.starts_with("TV.") && !value.starts_with("PC."))
        return false;
    const auto matrix = value.substr(3);
    return matrix == "601" || matrix == "709" || matrix == "FCC" || matrix == "240M";
}

std::u8string wide(const std::string &s)
{
    return std::u8string(s.begin(), s.end());
}

}

std::string recommendedMatrix(const VideoColour &colour, int width, int height)
{
    int space = colour.colorSpace;
    if (space == ffms_colour::kUnspecified)
        space = width > 1024 || height >= 600 ? ffms_colour::kBt709 : ffms_colour::kSmpte170m;
    const int range = colour.colorRange == ffms_colour::kRangeUnspecified ? ffms_colour::kRangeMpeg : colour.colorRange;
    if (space != ffms_colour::kBt709 && space != ffms_colour::kSmpte170m && space != ffms_colour::kBt470bg)
        return "None";
    return colourMatrixName(space, range);
}

bool untaggedMatrix(const VideoColour &colour)
{
    return colour.colorSpace == ffms_colour::kUnspecified;
}

bool hdrOrWideGamut(const VideoColour &colour)
{
    if (colour.transfer == ffms_colour::kTransferPq || colour.transfer == ffms_colour::kTransferHlg)
        return true;
    for (const int primaries : kWideGamutPrimaries)
        if (colour.primaries == primaries)
            return true;
    return false;
}

bool aspectRatioChanged(Resolution script, Resolution video)
{
    if (script.height == 0 || video.height == 0)
        return (script.height == 0) != (video.height == 0);
    const double scriptAspect = double(script.width) / script.height;
    const double videoAspect = double(video.width) / video.height;
    return std::abs(scriptAspect - videoAspect) / videoAspect > .01;
}

MatrixCheck checkMatrix(const core::Document &document, std::string_view recommended)
{
    MatrixCheck check;
    check.recommended = recommended;
    if (document.format() != core::SubtitleFormat::Ass)
        return check;
    const auto written = document.scriptInfo(u8"YCbCr Matrix");
    check.script = text(written);
    if (!written || !namesMatrix(check.script))
        check.question = MatrixQuestion::Missing;
    else if (check.script != "None" && check.script != recommended)
        check.question = MatrixQuestion::Mismatch;
    return check;
}

LayoutCheck checkLayoutResolution(const core::Document &document, Resolution video, LayoutResMismatch condition)
{
    LayoutCheck check;
    if (document.format() != core::SubtitleFormat::Ass)
        return check;
    check.layout = {intOf(document, u8"LayoutResX"), intOf(document, u8"LayoutResY")};
    check.play = {intOf(document, u8"PlayResX"), intOf(document, u8"PlayResY")};
    if (check.layout.width == 0 || check.layout.height == 0) {
        check.question = LayoutQuestion::Missing;
        return check;
    }
    if (check.layout.width == video.width && check.layout.height == video.height)
        return check;
    check.aspectChanged = aspectRatioChanged(check.layout, video);
    if (condition == LayoutResMismatch::Always || (condition == LayoutResMismatch::OnAspectChange && check.aspectChanged))
        check.question = LayoutQuestion::Mismatch;
    return check;
}

std::expected<void, CommandRefusal> setMatrix(EditSession &session, std::string_view matrix)
{
    if (session.document().format() != core::SubtitleFormat::Ass)
        return std::unexpected(CommandRefusal::Invalid);
    const auto value = wide(std::string(matrix));
    if (session.document().scriptInfo(u8"YCbCr Matrix") == value)
        return {};
    const auto ran = session.run(Command{std::string(kMatrixStep), session.revision(), {}, [&](core::Document &d) {
        return d.setScriptInfo(u8"YCbCr Matrix", value);
    }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

std::expected<void, CommandRefusal> setLayoutResolution(EditSession &session, Resolution layout)
{
    if (session.document().format() != core::SubtitleFormat::Ass || layout.width < 1 || layout.height < 1)
        return std::unexpected(CommandRefusal::Invalid);
    const auto x = wide(std::to_string(layout.width)), y = wide(std::to_string(layout.height));
    const auto &doc = session.document();
    if (doc.scriptInfo(u8"LayoutResX") == x && doc.scriptInfo(u8"LayoutResY") == y)
        return {};
    const auto ran = session.run(Command{std::string(kLayoutResolutionStep), session.revision(), {}, [&](core::Document &d) {
        return d.setScriptInfo(u8"LayoutResX", x) && d.setScriptInfo(u8"LayoutResY", y);
    }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

}
