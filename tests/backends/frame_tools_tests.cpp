#include "fake_host.h"
#include "script_client.h"

#include "hikari/application/document_overlay.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_save.h"

#include <QFile>
#include <gtest/gtest.h>

#include <cstring>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

constexpr int kW = 64, kH = 36;

const std::string kHeader =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 64\nPlayResY: 36\nScaledBorderAndShadow: yes\n\n"
    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
    "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
    "MarginL, MarginR, MarginV, Encoding\n"
    "Style: Default,Titillium Web,12,&H00FFFFFF,&H000000FF,&H00101010,&H00000000,0,0,0,0,100,100,0,0,1,1,0,5,0,0,0,1"
    "\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
const std::string kBox =
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(4,4)\\an7\\p1\\bord0\\c&H0000FF&}m 0 0 l 10 0 10 8 0 8{\\p0}\n";
const std::string kWord = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(46,20)}Hi\n";

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::string fromBase64(std::string_view text)
{
    return QByteArray::fromBase64(QByteArray(text.data(), qsizetype(text.size()))).toStdString();
}

class LibassRender final : public RenderHost {
public:
    std::optional<application::OverlayFrame> render(application::DocumentId, const core::Document &document,
                                                    std::int64_t frame, int width, int height) override
    {
        auto drawn = overlay.render(document, {fonts}, core::DocumentTime(frame * 40'000), width, height);
        return drawn ? std::optional(std::move(*drawn)) : std::nullopt;
    }

    application::FontLease fonts{"Titillium Web", testFont()};
    backends::LibassRenderer renderer;
    application::DocumentOverlay overlay{renderer, [](const core::Document &document) {
                                             return core::encodeAss(document, core::AssSaveOptions{.renderer = true});
                                         }};
};

class FrameTools : public ::testing::Test {
protected:
    FrameTools()
    {
        host.sessions.at(1) = std::make_unique<application::EditSession>(loadAss(kHeader + kBox + kWord));
    }

    application::OverlayFrame libassAloneAtFrame25(const std::string &events)
    {
        backends::LibassRenderer alone;
        EXPECT_TRUE(alone.prepare(application::RenderSnapshot{bytesOf(kHeader + events), {render.fonts}}));
        auto frame = alone.render(core::DocumentTime(1'000'000), kW, kH);
        EXPECT_TRUE(frame);
        return frame ? std::move(*frame) : application::OverlayFrame{};
    }

    static std::string onFrame25(const application::OverlayFrame &overlay)
    {
        std::string bgra(std::size_t(kW) * kH * 4, '\0');
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                char *p = bgra.data() + (y * kW + x) * 4;
                p[0] = char(x);
                p[2] = char(25);
                p[3] = char(255);
            }
        application::compositeOverlay(overlay, reinterpret_cast<std::uint8_t *>(bgra.data()), kW, kH, kW * 4);
        return bgra;
    }

    static Json rect(int x, int y, int width, int height)
    {
        return Json::object({{"x", Json(x)}, {"y", Json(y)}, {"width", Json(width)}, {"height", Json(height)}});
    }

    static Json scannedInk(const application::OverlayFrame &overlay)
    {
        int left = kW, right = -1, top = kH, bottom = -1;
        for (int y = 0; y < overlay.height && !overlay.empty; ++y)
            for (int x = 0; x < overlay.width; ++x)
                if (overlay.pixels[std::size_t(y) * overlay.stride + std::size_t(x) * 4 + 3]) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        return right < 0 ? Json() : rect(left, top, right - left + 1, bottom - top + 1);
    }

    Result call(std::string_view operation, const char *params)
    {
        return client.call(aiCaller(), operation, *Json::parse(params));
    }

    LibassRender render;
    FakeHost host{&render};
    Api api{host.host()};
    ScriptClient client{api};
};

TEST_F(FrameTools, AFrameWithOneLineIsLibassDrawingThatLineAlone)
{
    const auto word = libassAloneAtFrame25(kWord);
    ASSERT_FALSE(word.empty);
    const Result frame = call("video.frame", R"({"line": 2, "maxEdge": 4096})");
    ASSERT_TRUE(frame);
    EXPECT_EQ(*frame->find("frame"), Json(25));
    EXPECT_EQ(*frame->find("crop"), rect(0, 0, kW, kH));
    EXPECT_EQ(fromBase64(frame->find("data")->asString()), onFrame25(word));
}

TEST_F(FrameTools, ACropIsThatPartOfTheRenderedFrame)
{
    const std::string whole = onFrame25(libassAloneAtFrame25(kBox));
    const Result frame = call("video.frame", R"({"line": 1, "crop": {"x": 2, "y": 3, "width": 20, "height": 12}})");
    ASSERT_TRUE(frame);
    std::string part;
    for (int y = 3; y < 15; ++y)
        part += whole.substr((std::size_t(y) * kW + 2) * 4, 20 * 4);
    EXPECT_EQ(fromBase64(frame->find("data")->asString()), part);
}

TEST_F(FrameTools, InkBoundsAreWhereLibassDraws)
{
    const Json box = scannedInk(libassAloneAtFrame25(kBox));
    const Json word = scannedInk(libassAloneAtFrame25(kWord));
    ASSERT_TRUE(box.find("x"));
    const auto at = [&](const char *key) { return int(box.find(key)->asInt()); };
    EXPECT_TRUE(at("x") <= 4 && at("y") <= 4 && at("x") + at("width") >= 14 && at("y") + at("height") >= 12);
    EXPECT_LT(at("x") + at("width"), 30);
    ASSERT_TRUE(word.find("x"));
    EXPECT_GT(word.find("x")->asInt(), 30);
    EXPECT_EQ(*call("video.ink_bounds", R"({"line": 1})")->find("bounds"), box);
    EXPECT_EQ(*call("video.ink_bounds", R"({"line": 2})")->find("bounds"), word);
    EXPECT_EQ(*call("video.ink_bounds", R"({"frame": 25})")->find("bounds"), scannedInk(libassAloneAtFrame25(kBox + kWord)));
    EXPECT_EQ(*call("video.ink_bounds", R"({"frame": 60})")->find("bounds"), Json());
}

}
