#include "hikari/application/automation_services.h"
#include "hikari/application/resample.h"
#include "hikari/application/script_resolution.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"

#include <gtest/gtest.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return core::loadAss(b).document;
}

std::string saved(const core::Document &d)
{
    const auto bytes = core::encodeAss(d);
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::string text(const core::Document &d, std::size_t row)
{
    const auto &t = d.lines()[row]->text;
    return std::string(t.begin(), t.end());
}

constexpr std::string_view kScript =
    "[Script Info]\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,1.5,0,1,2,1,2,10,15,10,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(100,50)\\bord2\\fs20\\clip(m 0 0 l 10 0 10 10)}a{\\p1}m 0 0 l 4 4{\\p0}\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,10,0,7,,{\\fscx100\\xbord2\\yshad2\\move(10, 20,30,40,0,500)\\clip(1,2,3,4)}b\n"
    "Comment: 0,0:00:01.00,0:00:02.00,Default,,10,0,0,,{\\pos(100,50)}c\n";

}

TEST(Resample, ScalesStylesTagsDrawingsAndMargins)
{
    auto d = load(kScript);
    resizeSubtitles(d, 2.f, 2.f, false);
    const auto style = core::decodeStyles(d).front();
    EXPECT_EQ(style.fontsize, u8"40");
    EXPECT_EQ(style.outlineWidth, u8"4");
    EXPECT_EQ(style.shadow, u8"2");
    EXPECT_EQ(style.spacing, u8"3");
    EXPECT_EQ(style.marginLeft, u8"20");
    EXPECT_EQ(style.marginRight, u8"30");
    EXPECT_EQ(style.marginVertical, u8"20");
    EXPECT_EQ(style.scaleX, u8"100");
    EXPECT_EQ(text(d, 0), "{\\pos(200,100)\\bord4\\fs40\\clip(m 0 0 l 20 0 20 20)}a{\\p1}m 0 0 l 8 8{\\p0}");
    EXPECT_EQ(text(d, 1), "{\\fscx100\\xbord4\\yshad4\\move(20,40,60,80,0,500)\\clip(2,4,6,8)}b");
    EXPECT_EQ(d.lines()[1]->marginLeft.value, 20);
    EXPECT_EQ(d.lines()[1]->marginVertical.value, 14);
    EXPECT_EQ(text(d, 2), "{\\pos(100,50)}c");
    EXPECT_EQ(d.lines()[2]->marginLeft.value, 10);
    const auto file = saved(d);
    EXPECT_NE(file.find("Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,3,0,1,4,2,2,20,30,20,1\n"),
              std::string::npos)
        << file;
}

TEST(Resample, StretchScalesWidthThroughFscxAndFontSizeByTheLargerFactor)
{
    auto d = load(kScript);
    resizeSubtitles(d, 2.f, 1.f, true);
    const auto style = core::decodeStyles(d).front();
    EXPECT_EQ(style.fontsize, u8"20");
    EXPECT_EQ(style.scaleX, u8"200");
    EXPECT_EQ(style.outlineWidth, u8"2");
    EXPECT_EQ(style.marginLeft, u8"20");
    EXPECT_EQ(style.marginVertical, u8"10");
    EXPECT_EQ(text(d, 0), "{\\pos(200,50)\\bord2\\fs40\\clip(m 0 0 l 20 0 20 10)}a{\\p1}m 0 0 l 4 4{\\p0}");
    EXPECT_EQ(text(d, 1), "{\\fscx200\\xbord4\\yshad2\\move(20,20,60,40,0,500)\\clip(2,2,6,4)}b");
    auto plain = load(kScript);
    resizeSubtitles(plain, 2.f, 1.f, false);
    EXPECT_EQ(core::decodeStyles(plain).front().scaleX, u8"100");
    EXPECT_EQ(text(plain, 1).substr(0, 10), "{\\fscx100\\");
}

TEST(Resample, StretchPutsTheExtraScaleOnTheAxisThatGrewMore)
{
    constexpr std::string_view line = "{\\fscx50\\fscy80}a{\\p1}m 0 0 l 4 4{\\p0}";
    std::string script(kScript.substr(0, kScript.find("Dialogue:")));
    script += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,";
    script += line;
    script += "\n";

    EditSession taller{load(script)};
    ASSERT_TRUE(changeResolution(taller, {640, 360}, {960, 1080}, true, {.stretch = true}));
    const auto tallStyle = core::decodeStyles(taller.document()).front();
    EXPECT_EQ(tallStyle.fontsize, u8"30");
    EXPECT_EQ(tallStyle.scaleX, u8"100");
    EXPECT_EQ(tallStyle.scaleY, u8"200");
    EXPECT_EQ(text(taller.document(), 0), "{\\fscx50\\fscy160}a{\\p1}m 0 0 l 6 6{\\p0}");

    EditSession wider{load(script)};
    ASSERT_TRUE(changeResolution(wider, {640, 360}, {1920, 720}, true, {.stretch = true}));
    const auto wideStyle = core::decodeStyles(wider.document()).front();
    EXPECT_EQ(wideStyle.fontsize, u8"40");
    EXPECT_EQ(wideStyle.scaleX, u8"150");
    EXPECT_EQ(wideStyle.scaleY, u8"100");
    EXPECT_EQ(text(wider.document(), 0), "{\\fscx75\\fscy80}a{\\p1}m 0 0 l 8 8{\\p0}");
}

TEST(Resample, UnscalableValuesAreKeptAndReported)
{
    auto d = load(std::string(kScript.substr(0, kScript.find("Dialogue:"))) +
                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(a,5)\\clip(m 1 n 2 3)}x\n");
    std::vector<std::string> warnings;
    resizeSubtitles(d, 2.f, 2.f, false, [&](int row, const std::u16string &value, const std::u16string &tag) {
        warnings.push_back(std::to_string(row) + ":" + std::string(value.begin(), value.end()) + ":" +
                           std::string(tag.begin(), tag.end()));
    });
    EXPECT_EQ(text(d, 0), "{\\pos(a,10)\\clip(m 2 n 4 6)}x");
    EXPECT_EQ(warnings, (std::vector<std::string>{"1:n:clip", "1:a:pos"}));
}

TEST(Resample, TheTranslationIsWhatChanges)
{
    auto d = load("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n[Events]\n"
                  "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                  "Dialogue: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,{\\pos(1,1)}o\n"
                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(1,1)}t\n");
    resizeSubtitles(d, 2.f, 2.f, false);
    EXPECT_EQ(text(d, 0), "{\\pos(1,1)}o");
    const auto &tl = d.lines()[0]->translation;
    EXPECT_EQ(std::string(tl.begin(), tl.end()), "{\\pos(2,2)}t");
}

TEST(Resample, ScriptResolutionDefaults)
{
    EXPECT_EQ(scriptResolution(load("[Script Info]\nTitle: x\n")), (Resolution{1280, 720}));
    EXPECT_EQ(scriptResolution(load("[Script Info]\nPlayResY: 480\n")), (Resolution{853, 480}));
    EXPECT_EQ(scriptResolution(load("[Script Info]\nPlayResX: 1920\n")), (Resolution{1920, 1080}));
}

TEST(ScriptResolution, MissingValuesAreFoundWithoutWriting)
{
    const auto bare = load("[Script Info]\nTitle: x\n");
    EXPECT_EQ(writtenScriptResolution(bare), (Resolution{0, 0}));
    EXPECT_TRUE(lacksScriptResolution(writtenScriptResolution(bare)));
    EXPECT_TRUE(lacksScriptResolution(writtenScriptResolution(load("[Script Info]\nPlayResX: 1920\n"))));
    EXPECT_TRUE(lacksScriptResolution(writtenScriptResolution(load("[Script Info]\nPlayResX: 0\nPlayResY: 720\n"))));
    EXPECT_FALSE(lacksScriptResolution(writtenScriptResolution(load("[Script Info]\nPlayResX: 640\nPlayResY: 360\n"))));
    EXPECT_EQ(writtenScriptResolution(load("[Script Info]\nPlayResX: 1\nPlayResX: 640\nPlayResY: 720p\n")),
              (Resolution{640, 720}));
    EXPECT_EQ(legacyScriptResolution({0, 0}), (Resolution{1280, 720}));
    EXPECT_EQ(legacyScriptResolution({0, 480}), (Resolution{853, 480}));
    EXPECT_EQ(legacyScriptResolution({1920, -1}), (Resolution{1920, 1080}));
    EXPECT_FALSE(bare.scriptInfo(u8"PlayResX"));
    const std::vector<MacroInfoLine> staged{{"Title", "x"}, {"PlayResY", "480"}};
    EXPECT_EQ(writtenScriptResolution(staged), (Resolution{0, 480}));
}

TEST(ScriptResolution, TheSuggestionIsTheVideosElseLegacys)
{
    const auto bare = writtenScriptResolution(load("[Script Info]\nTitle: x\n"));
    EXPECT_EQ(suggestedScriptResolution(bare, Resolution{1920, 1080}), (Resolution{1920, 1080}));
    EXPECT_EQ(suggestedScriptResolution(bare, std::nullopt), (Resolution{1280, 720}));
    EXPECT_EQ(suggestedScriptResolution(bare, Resolution{0, 0}), (Resolution{1280, 720}));
    EXPECT_EQ(suggestedScriptResolution(writtenScriptResolution(load("[Script Info]\nPlayResY: 480\n")), std::nullopt),
              (Resolution{853, 480}));
    EXPECT_EQ(suggestedScriptResolution({0, 480}, Resolution{320, 240}), (Resolution{320, 240}));
    EXPECT_EQ(suggestedScriptResolution({1920, 0}, Resolution{320, 240}), (Resolution{320, 240}));
    EXPECT_EQ(suggestedScriptResolution({-1, 1080}, Resolution{720, 576}), (Resolution{720, 576}));
    EXPECT_EQ(suggestedScriptResolution({1920, 0}, Resolution{0, 240}), (Resolution{1920, 1080}));
}

TEST(ScriptResolution, SettingItIsOneUndoableStep)
{
    EditSession session{load("[Script Info]\nTitle: x\nPlayResY: 480\nScriptType: v4.00+\n\n[Events]\n"
                             "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n")};
    const auto before = saved(session.document());
    const auto steps = session.historySize();
    ASSERT_TRUE(setScriptResolution(session, {1920, 1080}));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, kScriptResolutionStep);
    EXPECT_TRUE(saved(session.document())
                    .starts_with("[Script Info]\nTitle: x\nPlayResY: 1080\nScriptType: v4.00+\nPlayResX: 1920\n\n[Events]"))
        << saved(session.document());
    ASSERT_TRUE(setScriptResolution(session, {1920, 1080}));
    EXPECT_EQ(session.historySize(), steps + 1);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(saved(session.document()), before);
    EXPECT_FALSE(setScriptResolution(session, {0, 720}));
    session.setReadOnly(true);
    EXPECT_FALSE(setScriptResolution(session, {1280, 720}));
    EXPECT_EQ(saved(session.document()), before);
}

TEST(ScriptResolution, StagedScriptInfoFollowsTheDocument)
{
    std::vector<MacroInfoLine> info{{"Title", "x"}, {"PlayResX", "1"}, {"PlayResX", "2"}, {"WrapStyle", "0"}};
    setStagedScriptResolution(info, {640, 360});
    EXPECT_EQ(info, (std::vector<MacroInfoLine>{{"Title", "x"}, {"PlayResX", "1"}, {"PlayResX", "640"},
                                                {"WrapStyle", "0"}, {"PlayResY", "360"}}));
}

TEST(Resample, ChangeResolutionIsOneStep)
{
    EditSession session{load(kScript)};
    ASSERT_TRUE(changeResolution(session, {640, 360}, {1280, 720}, true));
    EXPECT_EQ(session.history().back().name, "Changing subtitles resolution");
    const auto &d = session.document();
    EXPECT_EQ(d.scriptInfo(u8"PlayResX"), u8"1280");
    EXPECT_EQ(d.scriptInfo(u8"PlayResY"), u8"720");
    EXPECT_FALSE(d.scriptInfo(u8"LayoutResX"));
    EXPECT_EQ(text(d, 0).substr(0, 15), "{\\pos(200,100)\\");
    EditSession only{load(kScript)};
    ASSERT_TRUE(changeResolution(only, {640, 360}, {1920, 1080}, false));
    EXPECT_EQ(only.document().scriptInfo(u8"PlayResX"), u8"1920");
    EXPECT_FALSE(only.document().scriptInfo(u8"LayoutResX"));
    EXPECT_EQ(text(only.document(), 0).substr(0, 14), "{\\pos(100,50)\\");
}

std::string withLayout(std::string_view script, std::string_view layout)
{
    std::string s(script);
    return s.insert(s.find("\n\n"), layout);
}

TEST(Resample, LayoutResYStaysAndLayoutResXFollowsTheAspectRatio)
{
    EditSession same{load(withLayout(kScript, "\nLayoutResX: 640\nLayoutResY: 360"))};
    ASSERT_TRUE(changeResolution(same, {640, 360}, {1280, 720}, true));
    EXPECT_EQ(same.document().scriptInfo(u8"LayoutResX"), u8"640");
    EXPECT_EQ(same.document().scriptInfo(u8"LayoutResY"), u8"360");

    EditSession wider{load(withLayout(kScript, "\nLayoutResX: 640\nLayoutResY: 480"))};
    ASSERT_TRUE(changeResolution(wider, {640, 480}, {1920, 1080}, true, {.stretch = true}));
    EXPECT_EQ(wider.document().scriptInfo(u8"LayoutResX"), u8"853");
    EXPECT_EQ(wider.document().scriptInfo(u8"LayoutResY"), u8"480");

    EditSession onlyX{load(withLayout(kScript, "\nLayoutResX: 640"))};
    ASSERT_TRUE(changeResolution(onlyX, {640, 360}, {1280, 720}, true));
    EXPECT_EQ(onlyX.document().scriptInfo(u8"LayoutResX"), u8"640");
    EXPECT_FALSE(onlyX.document().scriptInfo(u8"LayoutResY"));

    EditSession unscaled{load(withLayout(kScript, "\nLayoutResX: 640\nLayoutResY: 360"))};
    ASSERT_TRUE(changeResolution(unscaled, {640, 360}, {1920, 1080}, false));
    EXPECT_EQ(unscaled.document().scriptInfo(u8"LayoutResX"), u8"640");
    EXPECT_EQ(unscaled.document().scriptInfo(u8"LayoutResY"), u8"360");
}

class FixedMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const double fs = std::strtod(style[2].c_str(), nullptr);
        TextExtents e;
        e.width = static_cast<float>(static_cast<double>(text.size()) * fs * 0.55);
        e.height = static_cast<float>(fs * 1.2);
        return e;
    }
};

constexpr std::string_view kSigns =
    "[Script Info]\nPlayResX: 1280\nPlayResY: 720\nWrapStyle: 0\n\n[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Sign,Arial,32,&H0000FFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,2,1,8,10,10,10,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

std::string signs(const std::vector<std::string> &texts)
{
    std::string s(kSigns);
    for (const std::string &t : texts)
        s += "Dialogue: 0,0:00:14.00,0:00:14.21,Sign,,0,0,0,," + t + "\n";
    return s;
}

double tag(const std::string &text, std::string_view name, double initial)
{
    const std::string head = text.substr(0, text.find('}'));
    for (std::size_t at = head.find('\\'); at != std::string::npos; at = head.find('\\', at + 1)) {
        const std::string_view rest = std::string_view(head).substr(at + 1);
        if (rest.starts_with(name) && rest.size() > name.size() &&
            (std::isdigit(static_cast<unsigned char>(rest[name.size()])) || rest[name.size()] == '-' ||
             rest[name.size()] == '('))
            return std::strtod(head.c_str() + at + 1 + name.size() + (rest[name.size()] == '(' ? 1 : 0), nullptr);
    }
    return initial;
}

double second(const std::string &text, std::string_view name)
{
    const std::size_t at = text.find("\\" + std::string(name) + "(");
    return at == std::string::npos ? NAN : std::strtod(text.c_str() + text.find(',', at) + 1, nullptr);
}

ResampleOptions measured(FixedMeasure &m, bool stretch = false)
{
    ResampleOptions o;
    o.stretch = stretch;
    o.measure.port = &m;
    return o;
}

TEST(Resample, ThreeDRotationsComeOutAsResamplePerspectiveWritesThem)
{
    // arch1t3cht's Resample Perspective 2.1.1 on this corpus Line: tests/fixtures/automation-thirdparty/expected.json
    FixedMeasure m;
    EditSession session{load(signs({"{\\fad(80,0)\\pos(533.333333,266.666667)\\frx10\\fry-5}fbf"}))};
    ASSERT_TRUE(changeResolution(session, {1280, 720}, {1920, 1080}, true, measured(m)));
    const std::string out = text(session.document(), 0);
    EXPECT_NEAR(tag(out, "pos", 0), 800, 0.01) << out;
    EXPECT_NEAR(second(out, "pos"), 400, 0.01) << out;
    EXPECT_NEAR(tag(out, "org", 0), 800, 0.01) << out;
    EXPECT_NEAR(second(out, "org"), 400, 0.01) << out;
    EXPECT_NEAR(tag(out, "fscx", 100), 99.789, 0.006) << out;
    EXPECT_NEAR(tag(out, "fscy", 100), 99.162, 0.006) << out;
    EXPECT_NEAR(tag(out, "fry", 0), -3.338, 0.0006) << out;
    EXPECT_NEAR(tag(out, "frx", 0), 6.718, 0.0006) << out;
    EXPECT_NEAR(tag(out, "fax", 0), -0.008, 0.0006) << out;
    EXPECT_NEAR(tag(out, "frz", 0), 0, 0.0006) << out;
    EXPECT_NEAR(tag(out, "xbord", 3), 2.99, 0.006) << out;
    EXPECT_NEAR(tag(out, "ybord", 3), 2.97, 0.006) << out;
    EXPECT_NE(out.find("\\fad(80,0)"), std::string::npos) << out;
    EXPECT_TRUE(out.ends_with("}fbf")) << out;
}

TEST(Resample, StretchedThreeDRotationsComeOutAsResamplePerspectiveWritesThem)
{
    // Resample Perspective 2.1.1 on this script, captured through the corpus harness on 2026-11-04.
    // Arch measured Arial; frx moves slightly with the measured box.
    FixedMeasure m;
    EditSession session{load(signs({"{\\fad(80,0)\\pos(600,400)\\fscx75\\frx10\\fry-5}fbf"}))};
    ASSERT_TRUE(changeResolution(session, {1440, 1080}, {1920, 1080}, true, measured(m, true)));
    const std::string out = text(session.document(), 0);
    EXPECT_NEAR(tag(out, "pos", 0), 800, 0.01) << out;
    EXPECT_NEAR(second(out, "pos"), 400, 0.01) << out;
    EXPECT_NEAR(tag(out, "org", 0), 800, 0.01) << out;
    EXPECT_NEAR(tag(out, "fscx", 100), 99.834, 0.006) << out;
    EXPECT_NEAR(tag(out, "fscy", 100), 100.005, 0.006) << out;
    EXPECT_NEAR(tag(out, "fry", 0), -3.754, 0.0006) << out;
    EXPECT_NEAR(tag(out, "frx", 0), 10.016, 0.002) << out;
    EXPECT_NEAR(tag(out, "fax", 0), -0.009, 0.0006) << out;
    EXPECT_NEAR(tag(out, "frz", 0), 0, 0.0006) << out;
    EXPECT_NEAR(tag(out, "xbord", 2), 2 * 1.33, 0.012) << out;
    EXPECT_NEAR(tag(out, "ybord", 2), 2, 0.006) << out;
}

visual::perspective::Quad quadOf(const core::Document &d, Resolution r, FixedMeasure &m, double z)
{
    visual::perspective::Reader reader;
    reader.document = &d;
    reader.width = r.width;
    reader.height = r.height;
    reader.measure.port = &m;
    visual::perspective::StyleCache styles;
    const auto state = visual::perspective::readLine(reader, *d.lines().front(), styles);
    EXPECT_TRUE(state);
    return state ? visual::perspective::textToQuad(state->transform, state->box, z) : visual::perspective::Quad{};
}

TEST(Resample, StretchingKeepsTheQuadOnTheStretchedFrame)
{
    FixedMeasure m;
    for (const std::string line : {"{\\pos(600,300)\\org(420,500)\\frx25\\fry-30\\frz12\\fscx80}Sign",
                                   "{\\an7\\pos(100,80)\\frz30\\fax0.2}Sign", "{\\pos(700,500)\\fry40\\bord3}Sign"}) {
        SCOPED_TRACE(line);
        const auto source = load(signs({line}));
        const auto before = quadOf(source, {1280, 720}, m, 312.5);
        EditSession session{source};
        ASSERT_TRUE(changeResolution(session, {1280, 720}, {1920, 1440}, true, measured(m, true)));
        const auto after = quadOf(session.document(), {1920, 1440}, m, 312.5);
        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_NEAR(after[i].x, before[i].x * 1.5, 0.05) << i << " " << text(session.document(), 0);
            EXPECT_NEAR(after[i].y, before[i].y * 2, 0.05) << i << " " << text(session.document(), 0);
        }
    }
}

TEST(Resample, KeepingTheAspectRatioKeepsTheShapeWhereItsPositionGoes)
{
    FixedMeasure m;
    const auto source = load(signs({"{\\pos(600,300)\\org(420,500)\\frx25\\fry-30\\frz12}Sign"}));
    const auto before = quadOf(source, {1280, 720}, m, 312.5);
    EditSession session{source};
    ASSERT_TRUE(changeResolution(session, {1280, 720}, {1920, 1440}, true, measured(m)));
    const auto after = quadOf(session.document(), {1920, 1440}, m, 312.5);
    const auto edge = [](const visual::perspective::Quad &q, std::size_t a, std::size_t b) {
        return visual::PointF{q[b].x - q[a].x, q[b].y - q[a].y};
    };
    for (const auto &[a, b] : {std::pair{0u, 1u}, {1u, 2u}, {2u, 3u}, {3u, 0u}}) {
        EXPECT_NEAR(edge(after, a, b).x, 1.5 * edge(before, a, b).x, 0.05) << text(session.document(), 0);
        EXPECT_NEAR(edge(after, a, b).y, 1.5 * edge(before, a, b).y, 0.05) << text(session.document(), 0);
    }
}

TEST(Resample, LinesTheResizeKeepsAreLeftAlone)
{
    FixedMeasure m;
    const std::vector<std::string> lines{"{\\pos(600,300)\\frz30}Turned about its position",
                                         "{\\pos(600,300)\\org(100,100)\\fax0.3}Sheared", "Plain"};
    EditSession uniform{load(withLayout(signs({"{\\pos(600,300)\\frx20}Tilted"}), "\nLayoutResX: 1280\nLayoutResY: 720"))};
    ASSERT_TRUE(changeResolution(uniform, {1280, 720}, {1920, 1080}, true, measured(m)));
    EXPECT_EQ(text(uniform.document(), 0), "{\\pos(900,450)\\frx20}Tilted");

    EditSession keep{load(signs(lines))};
    ASSERT_TRUE(changeResolution(keep, {1280, 720}, {1920, 1440}, true, measured(m)));
    EXPECT_EQ(text(keep.document(), 0), "{\\pos(900,600)\\frz30}Turned about its position");
    EXPECT_EQ(text(keep.document(), 1), "{\\pos(900,600)\\org(150,200)\\fax0.3}Sheared");
    EXPECT_EQ(text(keep.document(), 2), "Plain");

    EditSession off{load(signs({"{\\pos(600,300)\\frx20}Tilted"}))};
    ResampleOptions o = measured(m);
    o.adjustRotations = false;
    ASSERT_TRUE(changeResolution(off, {1280, 720}, {1920, 1080}, true, o));
    EXPECT_EQ(text(off.document(), 0), "{\\pos(900,450)\\frx20}Tilted");
}

TEST(Resample, LinesThatCannotKeepTheirShapeAreOnlyScaledAndReported)
{
    FixedMeasure m;
    EditSession session{load(signs({"{\\move(100,100,200,200)\\frx20}Moving", "{\\frx20\\t(\\fscx50)}Animated",
                                    "{\\pos(10,10)\\frx20}Fine"}))};
    ResampleOptions o = measured(m);
    std::vector<int> rows;
    o.unadjusted = [&rows](int row) { rows.push_back(row); };
    ASSERT_TRUE(changeResolution(session, {1280, 720}, {1920, 1080}, true, o));
    EXPECT_EQ(rows, (std::vector<int>{1, 2}));
    EXPECT_EQ(text(session.document(), 0), "{\\move(150,150,300,300)\\frx20}Moving");
    EXPECT_NE(text(session.document(), 2).find("\\org(15,15)"), std::string::npos) << text(session.document(), 2);

    rows.clear();
    EditSession unmeasured{load(signs({"{\\pos(10,10)\\frx20}Fine"}))};
    o.measure.port = nullptr;
    ASSERT_TRUE(changeResolution(unmeasured, {1280, 720}, {1920, 1080}, true, o));
    EXPECT_EQ(rows, (std::vector<int>{1}));
    EXPECT_EQ(text(unmeasured.document(), 0), "{\\pos(15,15)\\frx20}Fine");
}

TEST(Resample, AdjustedRotationsAreOneUndoStep)
{
    FixedMeasure m;
    EditSession session{load(signs({"{\\pos(600,300)\\frx20}Tilted"}))};
    const auto before = saved(session.document());
    const auto steps = session.historySize();
    ASSERT_TRUE(changeResolution(session, {1280, 720}, {1920, 1080}, true, measured(m)));
    EXPECT_NE(text(session.document(), 0).find("\\org("), std::string::npos);
    EXPECT_EQ(session.historySize(), steps + 1);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(saved(session.document()), before);
}
