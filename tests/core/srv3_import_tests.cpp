#include "hikari/core/ass_load.h"
#include "hikari/core/srv3.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

const std::filesystem::path kExport = HIKARI_EXPORT_FIXTURES;
const std::filesystem::path kImport = std::filesystem::path(HIKARI_IMPORT_FIXTURES) / "srv3";

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::span<const std::byte> bytes(std::string_view s)
{
    return std::as_bytes(std::span(s.data(), s.size()));
}

std::string imported(std::string_view ytt)
{
    const auto result = hikari::core::assFromSrv3(bytes(ytt));
    if (!result)
        return "refused: " + result.error();
    return {reinterpret_cast<const char *>(result->data()), result->size()};
}

std::string exported(const std::filesystem::path &script)
{
    const std::string text = readFile(script);
    const auto result = hikari::core::srv3FromAss(hikari::core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(text.data()), text.size())));
    return result ? std::string(reinterpret_cast<const char *>(result->data()), result->size()) : std::string();
}

std::vector<std::filesystem::path> ytts(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::path> out;
    for (const auto &entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".ytt")
            out.push_back(entry.path());
    std::ranges::sort(out);
    return out;
}

TEST(Srv3Import, EveryFileMatchesYtSubConvertersReverseConversionByteForByte)
{
    std::vector<std::filesystem::path> inputs = ytts(kExport / "srv3" / "golden");
    std::ranges::copy(ytts(kImport / "inputs"), std::back_inserter(inputs));
    ASSERT_EQ(inputs.size(), 35u);
    for (const auto &input : inputs) {
        SCOPED_TRACE(input.filename().string());
        EXPECT_EQ(imported(readFile(input)), readFile(kImport / "golden" / input.filename().replace_extension(".ass")));
    }
}

TEST(Srv3Import, FilesYtSubConverterRefusesAreRefusedWithItsReason)
{
    struct Case {
        std::string_view ytt, reason;
    };
    const Case cases[] = {
        {"", "Root element is missing."},
        {"hello", "Data at the root level is invalid."},
        {R"(<timedtext><body><p t="0" d="1000">x</s></body></timedtext>)", "The 'p' start tag does not match the end tag of 's'."},
        {R"( <?xml version="1.0"?><timedtext/>)", "Unexpected XML declaration."},
        {R"(<timedtext><body><p t="0" d="1000" t="5">x</p></body></timedtext>)", "'t' is a duplicate attribute name."},
        {R"(<timedtext><body><p t="0" d="1000">&e;</p></body></timedtext>)", "Reference to undeclared entity 'e'."},
        {R"(<a:timedtext/>)", "'a' is an undeclared prefix."},
        {R"(<timedtext><head><pen id="1" et="9"/></head></timedtext>)", "Specified argument was out of the range of valid values."},
        {R"(<timedtext><head><wp ap="9"/></head></timedtext>)", "Specified argument was out of the range of valid values."},
        {R"(<timedtext><head><pen of="5"/></head></timedtext>)", "Specified argument was out of the range of valid values."},
        {R"(<timedtext><head><pen fc="red"/></head></timedtext>)", "One of the identified items was in an invalid format."},
        {R"(<timedtext><head><pen id="-1"/></head></timedtext>)", "Index was out of range."},
        {R"(<timedtext><head><pen id="1" rb="1"/></head><body><p t="0" d="1000"><s p="1">a</s></p></body></timedtext>)", "Invalid ruby sequence"},
        {R"(<!DOCTYPE timedtext><timedtext/>)", "Document type declarations aren't supported."},
    };
    for (const Case &c : cases) {
        SCOPED_TRACE(std::string(c.ytt));
        const std::string result = imported(c.ytt);
        EXPECT_TRUE(result.starts_with("refused: ")) << result;
        EXPECT_NE(result.find(c.reason), std::string::npos) << result;
    }
}

TEST(Srv3Import, EmptyBodyAndOtherNamespacesGiveAScriptWithoutLines)
{
    for (std::string_view ytt : {R"(<timedtext format="3"/>)", R"(<timedtext xmlns="urn:x"><body><p>x</p></body></timedtext>)"}) {
        const std::string ass = imported(ytt);
        EXPECT_TRUE(ass.starts_with("\xEF\xBB\xBF[Script Info]\n")) << ass;
        EXPECT_TRUE(ass.ends_with("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n")) << ass;
    }
}

struct Event {
    std::int64_t start = 0, end = 0;
    std::u16string raw, text;
};

std::vector<Event> events(std::string_view ass)
{
    const hikari::core::LoadResult loaded = hikari::core::loadAss(bytes(ass));
    std::vector<Event> out;
    for (const hikari::core::LineRecord *line : loaded.document.lines()) {
        if (line->comment)
            continue;
        Event e{line->start.value.microseconds(), line->end.value.microseconds(), hikari::core::toUtf16(line->text), {}};
        e.text = hikari::core::project(e.raw).text;
        std::ranges::replace(e.text, u' ', u' ');
        while (!e.text.empty() && (e.text.back() == u' ' || e.text.back() == u'\n'))
            e.text.pop_back();
        out.push_back(std::move(e));
    }
    return out;
}

TEST(Srv3Import, ExportThenImportKeepsTheFixturesTextAndTiming)
{
    constexpr std::int64_t kOneFrame = 40'000;
    for (const char *name : {"plain.ass", "karaoke.ass", "typesetting.ass"}) {
        SCOPED_TRACE(name);
        const std::vector<Event> back = events(imported(exported(kExport / name)));
        ASSERT_FALSE(back.empty());
        std::size_t checked = 0;
        for (const Event &original : events(readFile(kExport / name))) {
            if (original.end <= original.start || original.text.empty() || original.text.contains(hikari::core::kProtectedMarker))
                continue;
            std::int64_t start = INT64_MAX, end = INT64_MIN;
            for (const Event &e : back) {
                if (!e.text.contains(original.text))
                    continue;
                start = std::min(start, e.start);
                end = std::max(end, e.end);
            }
            ASSERT_NE(start, INT64_MAX) << std::string(reinterpret_cast<const char *>(hikari::core::toUtf8(original.text).c_str()));
            EXPECT_LE(std::abs(start - original.start), kOneFrame);
            EXPECT_LE(std::abs(end - original.end), kOneFrame);
            ++checked;
        }
        EXPECT_GE(checked, 5u);
    }
}

TEST(Srv3Import, ExportThenImportKeepsEmphasisAndColourOnYouTubeStyles)
{
    const std::vector<Event> back = events(imported(exported(kExport / "plain.ass")));
    auto rawOf = [&](std::u16string_view text) {
        for (const Event &e : back)
            if (e.text.starts_with(text))
                return e.raw;
        return std::u16string();
    };
    EXPECT_TRUE(rawOf(u"You're late").contains(u"{\\i1}again{\\i0}"));
    EXPECT_TRUE(rawOf(u"Why does he").contains(u"\\i1"));
    EXPECT_TRUE(rawOf(u"Hurry").contains(u"\\b1}Hurry"));
    EXPECT_TRUE(rawOf(u"Chapter").contains(u"\\u1}Chapter"));
    EXPECT_TRUE(rawOf(u"Wrong Right").contains(u"\\c&H00FFFF&}tomorrow"));

    const std::string ass = imported(exported(kExport / "plain.ass"));
    const hikari::core::LoadResult loaded = hikari::core::loadAss(bytes(ass));
    for (const hikari::core::LineRecord *line : loaded.document.lines())
        EXPECT_TRUE(line->style.starts_with(u8"YT"));
}

}
