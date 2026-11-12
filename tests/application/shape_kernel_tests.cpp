#include "hikari/application/shape_kernel.h"

#include <gtest/gtest.h>

#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace shape = hikari::application::shape;
namespace yutils = hikari::application::shape::yutils;

namespace {

struct Golden {
    std::vector<std::string> head;
    std::string input;
    bool error = false;
    std::vector<std::string> out;
};

std::string unescape(std::string_view s)
{
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            r += s[i + 1] == 'n' ? '\n' : s[i + 1];
            ++i;
        } else {
            r += s[i];
        }
    }
    return r;
}

std::vector<Golden> readGoldens()
{
    std::ifstream in(HIKARI_SHAPE_GOLDENS);
    std::vector<Golden> all;
    Golden g;
    for (std::string line; std::getline(in, line);) {
        if (line.starts_with("case ")) {
            g = {};
            std::istringstream words(line.substr(5));
            for (std::string w; words >> w;)
                g.head.push_back(w);
        } else if (line.starts_with("in ")) {
            g.input = unescape(std::string_view(line).substr(3));
        } else if (line == "error") {
            g.error = true;
        } else if (line.starts_with("out ")) {
            g.out.push_back(unescape(std::string_view(line).substr(4)));
        } else if (line == "end") {
            all.push_back(g);
        }
    }
    return all;
}

double number(std::string_view s)
{
    double v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

std::vector<double> numbers(const std::string &s)
{
    std::vector<double> v;
    std::istringstream words(s);
    for (std::string w; words >> w;)
        v.push_back(number(w));
    return v;
}

std::string label(const Golden &g)
{
    std::string s;
    for (const auto &w : g.head)
        s += w + ' ';
    return s + "on \"" + g.input.substr(0, 80) + "\"";
}

void expectText(const Golden &g, const std::optional<std::string> &native)
{
    if (g.error) {
        EXPECT_FALSE(native) << label(g);
        return;
    }
    ASSERT_TRUE(native) << label(g);
    ASSERT_EQ(g.out.size(), 1u) << label(g);
    EXPECT_EQ(*native, g.out[0]) << label(g);
}

const std::vector<Golden> &goldens()
{
    static const std::vector<Golden> all = readGoldens();
    return all;
}

}

TEST(ShapeKernelYutils, GoldensCoverEveryRoutedFunction)
{
    std::map<std::string, int> count;
    for (const Golden &g : goldens())
        ++count[g.head[0]];
    for (const char *name : {"bounding", "flatten", "move", "transform", "to_outline", "to_pixels"})
        EXPECT_GE(count[name], 60) << name;
}

TEST(ShapeKernelYutils, BoundingMatchesTheLua)
{
    for (const Golden &g : goldens()) {
        if (g.head[0] != "bounding")
            continue;
        const auto native = yutils::bounding(g.input);
        ASSERT_TRUE(native) << label(g);
        ASSERT_EQ(g.out.size(), 1u);
        if (g.out[0] == "nil") {
            EXPECT_FALSE(native->bounds) << label(g);
            continue;
        }
        ASSERT_TRUE(native->bounds) << label(g);
        const auto v = numbers(g.out[0]);
        EXPECT_EQ(native->bounds->x0, v[0]) << label(g);
        EXPECT_EQ(native->bounds->y0, v[1]) << label(g);
        EXPECT_EQ(native->bounds->x1, v[2]) << label(g);
        EXPECT_EQ(native->bounds->y1, v[3]) << label(g);
    }
}

TEST(ShapeKernelYutils, FlattenMatchesTheLuaToTheByte)
{
    for (const Golden &g : goldens())
        if (g.head[0] == "flatten")
            expectText(g, yutils::flatten(g.input, number(g.head[1])));
}

TEST(ShapeKernelYutils, MoveMatchesTheLuaToTheByte)
{
    for (const Golden &g : goldens())
        if (g.head[0] == "move")
            expectText(g, yutils::move(g.input, number(g.head[1]), number(g.head[2])));
}

TEST(ShapeKernelYutils, TransformMatchesTheLuaToTheByte)
{
    for (const Golden &g : goldens()) {
        if (g.head[0] != "transform")
            continue;
        yutils::Matrix m{};
        for (int i = 0; i < 16; ++i)
            m[static_cast<std::size_t>(i)] = number(g.head[static_cast<std::size_t>(i) + 1]);
        expectText(g, yutils::transform(g.input, m));
    }
}

TEST(ShapeKernelYutils, ToOutlineMatchesTheLuaToTheByte)
{
    for (const Golden &g : goldens()) {
        if (g.head[0] != "to_outline")
            continue;
        const std::optional<double> wy = g.head[2] == "-" ? std::nullopt : std::optional(number(g.head[2]));
        const auto mode = g.head[3] == "miter" ? yutils::OutlineMode::Miter
                          : g.head[3] == "bevel" ? yutils::OutlineMode::Bevel
                                                 : yutils::OutlineMode::Round;
        expectText(g, yutils::toOutline(g.input, number(g.head[1]), wy, mode));
    }
}

TEST(ShapeKernelYutils, ToPixelsMatchesTheLuaCoverage)
{
    for (const Golden &g : goldens()) {
        if (g.head[0] != "to_pixels")
            continue;
        const auto native = yutils::toPixels(g.input);
        if (g.error) {
            EXPECT_FALSE(native) << label(g);
            continue;
        }
        ASSERT_TRUE(native) << label(g);
        ASSERT_EQ(native->size(), g.out.size()) << label(g);
        for (std::size_t i = 0; i < g.out.size(); ++i) {
            const auto v = numbers(g.out[i]);
            EXPECT_EQ((*native)[i].alpha, v[0]) << label(g) << " pixel " << i;
            EXPECT_EQ((*native)[i].x, v[1]) << label(g) << " pixel " << i;
            EXPECT_EQ((*native)[i].y, v[2]) << label(g) << " pixel " << i;
        }
    }
}

TEST(ShapeKernelYutils, LeavesNumbersLuaReadsDifferentlyToTheLua)
{
    EXPECT_FALSE(yutils::flatten("m 0x10 0 b 1 2 3 4 5 6"));
    EXPECT_FALSE(yutils::bounding("m 1e3 0 l 5 5"));
    EXPECT_FALSE(yutils::move("m inf 0 l 5 5", 1, 1));
    EXPECT_FALSE(yutils::move("m -0 0 l 5 5", 1, 1));
    EXPECT_FALSE(yutils::move("m +1 0 l 5 5", 1, 1));
    EXPECT_EQ(yutils::move("m 1 2 l 3 4", 1, 1), "m 2 3 l 4 5");
}

TEST(ShapeKernelGeometry, ReadsLinesCurvesAndSplines)
{
    const auto square = shape::readDrawing("m 0 0 l 10 0 10 10 0 10");
    ASSERT_EQ(square.size(), 1u);
    EXPECT_EQ(square[0].size(), 4u);
    const auto curve = shape::readDrawing("m 0 0 b 0 -10 10 -10 10 0", 0.1);
    ASSERT_EQ(curve.size(), 1u);
    EXPECT_GT(curve[0].size(), 4u);
    EXPECT_EQ(curve[0].back(), (shape::Point{10, 0}));
    const auto spline = shape::readDrawing("m 0 0 s 10 0 10 10 0 10 c");
    ASSERT_EQ(spline.size(), 1u);
    EXPECT_GT(spline[0].size(), 4u);
    EXPECT_TRUE(shape::readDrawing("l 5 5 10 10").empty());
}

double area(const shape::Contours &contours)
{
    double a = 0;
    for (const auto &c : contours)
        for (std::size_t i = 0; i < c.size(); ++i) {
            const auto &p = c[i], &q = c[(i + 1) % c.size()];
            a += p.x * q.y - q.x * p.y;
        }
    return std::abs(a) / 2;
}

TEST(ShapeKernelGeometry, CombinesTwoSquares)
{
    const auto a = shape::readDrawing("m 0 0 l 10 0 10 10 0 10");
    const auto b = shape::readDrawing("m 5 5 l 15 5 15 15 5 15");
    EXPECT_NEAR(area(shape::combine(a, b, shape::Combine::Unite)), 175, 1e-6);
    EXPECT_NEAR(area(shape::combine(a, b, shape::Combine::Intersect)), 25, 1e-6);
    EXPECT_NEAR(area(shape::combine(a, b, shape::Combine::Subtract)), 75, 1e-6);
    const auto x = shape::combine(a, b, shape::Combine::Exclude);
    EXPECT_EQ(x.size(), 2u);
    double sum = 0;
    for (const auto &c : x)
        sum += area({c});
    EXPECT_NEAR(sum, 150, 1e-6);
}

TEST(ShapeKernelGeometry, OffsetsGrowAndShrink)
{
    const auto square = shape::readDrawing("m 0 0 l 10 0 10 10 0 10");
    EXPECT_NEAR(area(shape::offset(square, 2, shape::Join::Miter)), 196, 1e-6);
    EXPECT_NEAR(area(shape::offset(square, -2, shape::Join::Miter)), 36, 1e-6);
    const double round = area(shape::offset(square, 2, shape::Join::Round));
    EXPECT_NEAR(round, 100 + 80 + 4 * 3.14159265, 0.5);
    EXPECT_TRUE(shape::offset(square, -6, shape::Join::Round).empty());
}

TEST(ShapeKernelGeometry, WritesShortDrawings)
{
    EXPECT_EQ(shape::writeDrawing({{{0, 0}, {10.5, 0}, {10.125, -0.0001}}}), "m 0 0 l 10.5 0 10.13 0");
    EXPECT_EQ(shape::writeDrawing(shape::shifted({{{1, 1}, {2, 2}}}, 1, -1)), "m 2 0 l 3 1");
}
