#pragma once

#include "hikari/application/motion_data.h"

#include "hikari/core/style.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::motion {

struct Props {
    std::string fontname;
    double fontsize = 0, spacing = 0, scaleX = 100, scaleY = 100, angle = 0, outline = 0, shadow = 0;
    int align = 2;
    int marginL = 0, marginR = 0, marginV = 0;
    std::array<std::array<double, 3>, 4> bgr{};
    std::array<double, 4> alpha{};
};
Props propsOf(const core::StyleValues &style);

struct Piece {
    bool block = false;
    std::string text;
    std::vector<std::string> tags;
};
std::vector<Piece> parseText(std::string_view text);
std::string renderText(const std::vector<Piece> &pieces);

struct Track {
    std::vector<double> x, y, scale, turn;
    std::size_t ref = 0;
    struct Frame {
        double x = 0, y = 0, ratio = 1, turn = 0;
    };
    Frame at(std::size_t index) const;
    std::array<double, 2> follow(double px, double py, const Frame &frame) const;
};
Track resolveTrack(const MotionTrack &data, const MotionFollow &follow, double resX, double resY);

struct Setup {
    const Track *main = nullptr, *rect = nullptr, *vector = nullptr;
    MotionOptions options;
    double resX = 0, resY = 0;
    std::map<std::string, Props> styles;
    bool followsMain() const;
    bool touchesClips() const;
    bool toVector() const;
};

struct Move {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    double t1 = 0, t2 = 0;
    std::array<double, 2> at(double time) const;
};

struct PreparedLine {
    std::vector<Piece> pieces;
    Props props;
    std::optional<Move> move;
    bool hasOrg = false, hasClip = false;
    std::int64_t duration = 0;
    bool linear(const Setup &setup) const;
};
PreparedLine prepareLine(std::string_view text, std::int64_t duration, const Props &props, const Setup &setup,
                         int lineMarginL, int lineMarginR, int lineMarginV);

struct FrameTime {
    std::size_t index = 0;
    std::int64_t delta = 0;
    std::int64_t evaluate = 0;
};
std::string frameText(const PreparedLine &line, const Setup &setup, const FrameTime &time);

struct LinearSpan {
    std::size_t first = 0, last = 0;
    std::int64_t begin = 0, end = 0;
};
std::string linearText(const PreparedLine &line, const Setup &setup, const LinearSpan &span);

std::string finishFrame(std::string_view text, double karaokeShift);
std::string finishLinear(std::string_view text);

std::string formatG(double value);
std::string formatLua(double value);
double round2(double value);

}
