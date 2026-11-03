#include "hikari/plugins/package.h"

#include "test_signer.h"

#include <gtest/gtest.h>

using namespace hikari::plugins;
using hikari::plugins::test::bytes;
using Kind = PackageProblem::Kind;

namespace {

constexpr std::string_view kManifest = R"({"id": "com.example.hello", "name": "Hello", "version": "1.0.0",
    "api": 1, "main": "lua/main.lua", "authors": ["Ana"]})";

std::vector<PackageFile> plugin(std::vector<PackageFile> extra = {})
{
    std::vector<PackageFile> files{{"hikari-plugin.json", bytes(kManifest)}, {"lua/main.lua", bytes("return {}")}};
    files.insert(files.end(), extra.begin(), extra.end());
    return files;
}

PackageProblem problem(std::vector<PackageFile> files)
{
    const auto result = openPackage(std::move(files), 1);
    EXPECT_FALSE(result);
    return result ? PackageProblem{} : result.error();
}

std::string bigEndian(std::uint32_t value, int count)
{
    std::string out;
    for (int shift = (count - 1) * 8; shift >= 0; shift -= 8)
        out += char((value >> shift) & 0xFF);
    return out;
}

std::vector<std::byte> png(std::uint32_t width, std::uint32_t height, std::size_t size = 0)
{
    std::string s = std::string("\x89PNG\r\n\x1A\n", 8) + bigEndian(13, 4) + "IHDR" + bigEndian(width, 4) +
                    bigEndian(height, 4) + std::string("\x08\x06\0\0\0", 5);
    s.resize(std::max(s.size(), size), '\0');
    return bytes(s);
}

std::vector<std::byte> jpeg(std::uint32_t width, std::uint32_t height, bool frame = true)
{
    std::string s("\xFF\xD8\xFF\xE0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00", 20);
    if (frame)
        s += std::string("\xFF\xC2\x00\x11\x08", 5) + bigEndian(height, 2) + bigEndian(width, 2) + '\x03' + std::string(9, '\0');
    s += std::string("\xFF\xDA\x00\x02\xFF\xD9", 6);
    return bytes(s);
}

std::string svg(std::string_view body, std::string_view root = R"(viewBox="0 0 24 24")")
{
    return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)") + "\n<svg xmlns=\"http://www.w3.org/2000/svg\" " +
           std::string(root) + ">" + std::string(body) + "</svg>\n";
}

std::vector<PackageFile> withMedia(std::string_view fields, std::vector<PackageFile> extra)
{
    std::string manifest(kManifest);
    manifest.insert(manifest.rfind('}'), std::string(", ") + std::string(fields));
    std::vector<PackageFile> files{{"hikari-plugin.json", bytes(manifest)}, {"lua/main.lua", bytes("return {}")}};
    files.insert(files.end(), extra.begin(), extra.end());
    return files;
}

PackageProblem mediaProblem(std::string_view fields, std::vector<PackageFile> extra)
{
    PackageProblem p = problem(withMedia(fields, std::move(extra)));
    EXPECT_EQ(p.kind, Kind::Media) << fields;
    return p;
}

std::string iconRefusal(std::string path, std::vector<std::byte> data)
{
    const PackageProblem p = mediaProblem(R"("icon": ")" + path + R"(")", {{path, std::move(data)}});
    EXPECT_EQ(p.path, path);
    return p.detail;
}

std::string svgRefusal(std::string_view text)
{
    return iconRefusal("icon.svg", bytes(text));
}

std::string shotRefusal(std::string path, std::vector<std::byte> data)
{
    const PackageProblem p = mediaProblem(R"("screenshots": [{"path": ")" + path + R"(", "caption": "c"}])", {{path, std::move(data)}});
    EXPECT_EQ(p.path, path);
    return p.detail;
}

bool opens(std::string_view fields, std::vector<PackageFile> extra)
{
    const auto result = openPackage(withMedia(fields, std::move(extra)), 1);
    EXPECT_TRUE(result) << fields << ": " << (result ? std::string() : result.error().path + " " + result.error().detail);
    return bool(result);
}

TEST(PluginPackage, AWellFormedPackageOpens)
{
    const auto package = openPackage(plugin({{"README.md", bytes("hi")}, {"lua/lib/util.lua", {}}}), 1);
    ASSERT_TRUE(package);
    EXPECT_EQ(package->manifest.main, "lua/main.lua");
    EXPECT_EQ(package->files.size(), 4u);
}

TEST(PluginPackage, PathsThatEscapeOrCannotExistAreRefused)
{
    for (const char *path : {"../evil.lua", "/etc/passwd", "lua/../../x", "a\\b.lua", "C:x", "lua//x", "lua/./x",
                             "CON", "nul.txt", "lpt1.lua", "trailing.", "trailing ", "lua/", "", "a\nb"}) {
        const PackageProblem p = problem(plugin({{path, bytes("x")}}));
        EXPECT_EQ(p.kind, Kind::BadPath) << path;
        EXPECT_EQ(p.path, path);
    }
}

TEST(PluginPackage, PathsThatCollideAreRefused)
{
    EXPECT_EQ(problem(plugin({{"LUA/MAIN.lua", {}}})).kind, Kind::BadPath);
    EXPECT_EQ(problem(plugin({{"lua", {}}})).path, "lua");
}

TEST(PluginPackage, AMissingManifestOrMainIsRefused)
{
    EXPECT_EQ(problem({{"lua/main.lua", {}}}).kind, Kind::NoManifest);
    EXPECT_EQ(problem({{"sub/hikari-plugin.json", bytes(kManifest)}, {"lua/main.lua", {}}}).kind, Kind::NoManifest);
    const PackageProblem missing = problem({{"hikari-plugin.json", bytes(kManifest)}});
    EXPECT_EQ(missing.kind, Kind::MissingMain);
    EXPECT_EQ(missing.path, "lua/main.lua");
}

TEST(PluginPackage, AManifestProblemCarriesThrough)
{
    const PackageProblem p = problem({{"hikari-plugin.json", bytes("[]")}, {"lua/main.lua", {}}});
    EXPECT_EQ(p.kind, Kind::Manifest);
    EXPECT_EQ(p.manifest.kind, ManifestProblem::Kind::Schema);
}

TEST(PluginPackage, OversizePackagesAreRefused)
{
    std::vector<PackageFile> many = plugin();
    for (std::size_t i = 0; i < kMaxPackageFiles; ++i)
        many.push_back({"f" + std::to_string(i), {}});
    EXPECT_EQ(problem(std::move(many)).kind, Kind::TooLarge);
    EXPECT_EQ(problem(plugin({{"big.bin", std::vector<std::byte>(kMaxPackageBytes)}})).kind, Kind::TooLarge);
}

TEST(PluginPackage, MediaThatFollowsTheRulesOpens)
{
    EXPECT_TRUE(opens(R"("icon": "media/icon.png", "screenshots": [{"path": "media/a.png", "caption": "A"},
        {"path": "b.jpg", "caption": "B"}, {"path": "c.png", "caption": "C"}, {"path": "d.jpg", "caption": "D"}])",
                      {{"media/icon.png", png(256, 256, 256 * 1024)},
                       {"media/a.png", png(1920, 1080, 2 * 1024 * 1024)},
                       {"b.jpg", jpeg(1280, 720)},
                       {"c.png", png(1366, 768)},
                       {"d.jpg", jpeg(1600, 900)},
                       {"README.md", bytes(std::string(128 * 1024 - 4, 'a') + "\xC3\xA9\xC3\xA9")}}));
    EXPECT_TRUE(opens(R"("icon": "icon.svg")",
                      {{"icon.svg", bytes(svg(R"(<!-- <script> --><defs><linearGradient id="g"/></defs>)"
                                              R"~(<use href="#g" xlink:href="#g" style="fill: url( '#g')"/>)~"
                                              R"(<style><![CDATA[ rect { fill: url(#g) } ]]></style><svg:rect width="1"/>)",
                                              R"(xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0,0,48,48")"))}}));
    EXPECT_TRUE(opens(R"("summary": "Hi")", {{"readme.md", bytes("\xFF")}, {"docs/README.md", bytes("\xFF")}}));
}

TEST(PluginPackage, MediaPathsMustNameAPackageFileExactly)
{
    const std::string missing = "names no file in the package (the case must match)";
    EXPECT_EQ(mediaProblem(R"("icon": "icon.png")", {}).detail, missing);
    const PackageProblem cased = mediaProblem(R"("icon": "Icon.png")", {{"icon.png", png(256, 256)}});
    EXPECT_EQ(cased.path, "Icon.png");
    EXPECT_EQ(cased.detail, missing);
    EXPECT_EQ(mediaProblem(R"("screenshots": [{"path": "s.png", "caption": "c"}])", {}).detail, missing);
    const PackageProblem repeated = mediaProblem(
        R"("screenshots": [{"path": "s.png", "caption": "a"}, {"path": "s.png", "caption": "b"}])", {{"s.png", png(1280, 720)}});
    EXPECT_EQ(repeated.path, "s.png");
    EXPECT_EQ(repeated.detail, "repeats another screenshot");
}

TEST(PluginPackage, APngIconIsARealPngOf256By256AndAt256KiB)
{
    std::vector<std::byte> notPng = png(256, 256);
    notPng[1] = std::byte{'Q'};
    EXPECT_EQ(iconRefusal("icon.png", notPng), "isn't a PNG");
    std::vector<std::byte> noHeader = png(256, 256);
    noHeader[12] = std::byte{'X'};
    EXPECT_EQ(iconRefusal("icon.png", noHeader), "isn't a PNG");
    std::vector<std::byte> shortHeader = png(256, 256);
    shortHeader[11] = std::byte{12};
    EXPECT_EQ(iconRefusal("icon.png", shortHeader), "isn't a PNG");
    EXPECT_EQ(iconRefusal("icon.png", bytes("\x89PNG\r\n\x1A\n")), "isn't a PNG");
    EXPECT_EQ(iconRefusal("icon.png", jpeg(256, 256)), "isn't a PNG");
    EXPECT_EQ(iconRefusal("icon.png", png(128, 128)), "is 128x128 px, not 256x256 px");
    EXPECT_EQ(iconRefusal("icon.png", png(256, 255)), "is 256x255 px, not 256x256 px");
    EXPECT_EQ(iconRefusal("icon.png", png(256, 256, 256 * 1024 + 1)), "is larger than 256 KiB");
}

TEST(PluginPackage, AnSvgIconIsSmallSquareUtf8AndInert)
{
    EXPECT_EQ(svgRefusal(svg(std::string(64 * 1024, ' '))), "is larger than 64 KiB");
    EXPECT_EQ(svgRefusal(svg("<text>\xC3</text>")), "isn't UTF-8");
    EXPECT_EQ(svgRefusal(R"(<html viewBox="0 0 1 1"><svg/></html>)"), "has a root element other than <svg>");
    EXPECT_EQ(svgRefusal("just text"), "has no <svg> root element");
    EXPECT_EQ(svgRefusal(svg("", "")), "has no square viewBox on its <svg>");
    EXPECT_EQ(svgRefusal(svg("", R"(viewBox="0 0 24 12")")), "has no square viewBox on its <svg>");
    EXPECT_EQ(svgRefusal(svg("", R"(viewBox="0 0 0 0")")), "has no square viewBox on its <svg>");
    EXPECT_EQ(svgRefusal(svg("", R"(viewBox="0 0 24")")), "has no square viewBox on its <svg>");
    EXPECT_EQ(svgRefusal(svg("", R"(viewBox="0 0 24 24 24")")), "has no square viewBox on its <svg>");
    EXPECT_EQ(svgRefusal(svg("", R"(viewBox="0 0 a a")")), "has no square viewBox on its <svg>");
    EXPECT_EQ(svgRefusal(svg("", R"(viewbox="0 0 24 24")")), "has no square viewBox on its <svg>");
    EXPECT_EQ(svgRefusal(svg("<g><script>alert(1)</script></g>")), "holds a <script> element");
    EXPECT_EQ(svgRefusal(svg("<svg:SCRIPT/>")), "holds a <script> element");
    EXPECT_EQ(svgRefusal(svg("<foreignObject><div/></foreignObject>")), "holds a <foreignObject> element");
    EXPECT_EQ(svgRefusal(svg("", R"~(viewBox="0 0 24 24" onload="x()")~")), "has an onload attribute");
    EXPECT_EQ(svgRefusal(svg(R"(<rect ONCLICK='x()'/>)")), "has an ONCLICK attribute");
    EXPECT_EQ(svgRefusal(svg(R"(<a href="https://example.com"/>)")), "has an href that points outside the file");
    EXPECT_EQ(svgRefusal(svg(R"(<image xlink:href="data:image/png;base64,AA"/>)")), "has an xlink:href that points outside the file");
    EXPECT_EQ(svgRefusal(svg(R"~(<a href="&#106;avascript:x()"/>)~")), "has an href that points outside the file");
    EXPECT_EQ(svgRefusal(svg(R"~(<rect style="fill: url(https://example.com/a)"/>)~")), "has a url( that points outside the file");
    EXPECT_EQ(svgRefusal(svg(R"(<style>@font-face { src: URL("f.woff") }</style>)")), "has a url( that points outside the file");
    EXPECT_EQ(svgRefusal(svg(R"~(<rect style="fill: u&#x72;l(x.svg)"/>)~")), "has a url( that points outside the file");
    EXPECT_EQ(svgRefusal(svg(R"~(<rect fill="url()"/>)~")), "has a url( that points outside the file");
    EXPECT_EQ(svgRefusal(R"~(<!DOCTYPE svg [<!ATTLIST svg onload CDATA "x()">]><svg viewBox="0 0 1 1"/>)~"),
              "declares entities or attributes in its DOCTYPE");
    for (const char *broken : {"<svg viewBox=\"0 0 1 1\"", "<svg viewBox=0>", "<svg viewBox=\"0 0 1 1\"><!-- open",
                               "<svg viewBox=\"0 0 1 1\" x>", "<>", "<svg viewBox=\"0 0 1 1\"><![CDATA[ x"})
        EXPECT_EQ(svgRefusal(broken), "isn't well-formed XML") << broken;
}

TEST(PluginPackage, AScreenshotIsA16By9PngOrJpgFrom720pTo1080p)
{
    EXPECT_EQ(shotRefusal("s.png", jpeg(1280, 720)), "isn't a PNG");
    EXPECT_EQ(shotRefusal("s.jpg", png(1280, 720)), "isn't a JPEG");
    EXPECT_EQ(shotRefusal("s.jpg", jpeg(1280, 720, false)), "has no JPEG frame header");
    EXPECT_EQ(shotRefusal("s.jpg", bytes("\xFF\xD8\xFF")), "has no JPEG frame header");
    const std::string rule = "; a screenshot is 16:9, from 1280x720 to 1920x1080 px";
    EXPECT_EQ(shotRefusal("s.png", png(1279, 719)), "is 1279x719 px" + rule);
    EXPECT_EQ(shotRefusal("s.png", png(1921, 1081)), "is 1921x1081 px" + rule);
    EXPECT_EQ(shotRefusal("s.jpg", jpeg(2560, 1440)), "is 2560x1440 px" + rule);
    EXPECT_EQ(shotRefusal("s.png", png(1280, 800)), "is 1280x800 px" + rule);
    EXPECT_EQ(shotRefusal("s.jpg", jpeg(1440, 1080)), "is 1440x1080 px" + rule);
    EXPECT_EQ(shotRefusal("s.png", png(1368, 768)), "is 1368x768 px" + rule);
    EXPECT_EQ(shotRefusal("s.png", png(1280, 720, 2 * 1024 * 1024 + 1)), "is larger than 2 MiB");
}

TEST(PluginPackage, TheIconAndScreenshotsComeToAt8MiB)
{
    std::string fields = R"("icon": "icon.png", "screenshots": [)";
    std::vector<PackageFile> extra{{"icon.png", png(256, 256, 256 * 1024)}};
    for (int i = 0; i < 4; ++i) {
        const std::string path = "s" + std::to_string(i) + ".png";
        fields += (i ? ", " : "") + (R"({"path": ")" + path + R"(", "caption": "c"})");
        extra.push_back({path, png(1920, 1080, i < 3 ? 2 * 1024 * 1024 : 2 * 1024 * 1024 - 256 * 1024)});
    }
    EXPECT_TRUE(opens(fields + "]", extra));
    fields += R"(, {"path": "last.jpg", "caption": "c"}])";
    extra.push_back({"last.jpg", jpeg(1280, 720)});
    const PackageProblem p = mediaProblem(fields, extra);
    EXPECT_EQ(p.path, "last.jpg");
    EXPECT_EQ(p.detail, "takes the icon and screenshots past 8 MiB together");
}

TEST(PluginPackage, AReadmeIsUtf8AndAt128KiB)
{
    const PackageProblem large = mediaProblem(R"("summary": "Hi")", {{"README.md", bytes(std::string(128 * 1024 + 1, 'a'))}});
    EXPECT_EQ(large.path, "README.md");
    EXPECT_EQ(large.detail, "is larger than 128 KiB");
    EXPECT_EQ(mediaProblem(R"("summary": "Hi")", {{"README.md", bytes("caf\xE9")}}).detail, "isn't UTF-8");
    EXPECT_EQ(problem(plugin({{"README.md", bytes("\xC0\xAF")}})).kind, Kind::Media);
}

}
