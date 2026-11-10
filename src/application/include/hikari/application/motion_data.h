#pragma once

#include "hikari/application/aegisub_frame_times.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/motion_fit.h"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// After Effects 6.0 keyframe data, one row a frame. Rotation is in After Effects' degrees, the
// opposite sense of \frz.
struct MotionTrack {
    std::vector<double> x, y, scaleX, scaleY, rotation;
    double fps = 0;
    int width = 0, height = 0;
    int frames() const { return static_cast<int>(x.size()); }
};
// nullopt unless position, scale and rotation all hold the same, non-zero number of frames.
std::optional<MotionTrack> parseMotionData(std::u8string_view text);
std::u8string writeMotionData(const MotionTrack &track);

struct MotionFollow {
    int relativeFrame = 1;
    bool x = true, y = true, scale = true, rotation = false;
    bool rectClips = true, vectorClips = true, rectToVector = false;
};

struct MotionOptions {
    MotionFollow main;
    bool absolute = false, origin = false;
    bool border = true, shadow = true, blur = true;
    double blurScale = 1;
    bool clipsOnly = false;
    bool bakeTransforms = true;
    bool linear = false;
    std::optional<MotionTrack> clipTrack;
    MotionFollow clip;
};

enum class MotionProblem { NoLines, Length, NotTracked };
struct MotionCheck {
    std::optional<MotionProblem> problem;
    std::int64_t firstFrame = 0;
    int frames = 0;
};
MotionCheck checkMotion(const EditSession &session, const AegisubFrameTimes &times, int dataFrames,
                        const LineVisible &visible = {});

// Each selected shown Dialogue Line becomes one Line a frame (or one Line with \move and \t when
// linear), placed where the first of them was. One step, "Apply motion data"; the new Lines are
// selected. The Lines keep Aegisub-Motion's "a-mo" extradata, so either Revert undoes it.
std::expected<void, CommandRefusal> applyMotion(EditSession &session, const AegisubFrameTimes &times,
                                                const MotionTrack &track, const MotionOptions &options,
                                                const LineVisible &visible = {});

// Every Line sharing an "a-mo" id with a selected Line goes back to the one original Line, spanning
// all of them. One step, "Revert motion data".
std::expected<void, CommandRefusal> revertMotion(EditSession &session, const LineVisible &visible = {});

enum class DeriveProblem { TooFewLines, NotFrameByFrame, NoPosition };
struct DeriveFailure {
    DeriveProblem problem;
    int row = 0;
};
// The selected frame-by-frame Lines' first-block \pos, \fscx, \fscy and \frz as motion data at the
// script's resolution.
std::expected<MotionTrack, DeriveFailure> deriveMotion(const EditSession &session, const AegisubFrameTimes &times,
                                                      const LineVisible &visible = {});

// One fit a series: x, y, scale x, scale y, rotation.
using SeriesFits = std::array<PolynomialFit, 5>;
FitConfidence worstConfidence(const SeriesFits &fits);

// The track with `before` frames fitted ahead of its first and `after` behind its last, each series on
// its own fit.
struct ExtendedTrack {
    MotionTrack track;
    std::optional<SeriesFits> before, after;
    FitConfidence confidence() const;
};
ExtendedTrack extendTrack(const MotionTrack &track, int before, int after);

enum class ExtendProblem { TooFewLines, NotFrameByFrame, Animated, NoPosition, BeforeVideo, Refused };
struct ExtendFailure {
    ExtendProblem problem;
    int row = 0;
};
// The selected frame-by-frame Lines' first-block \pos, \fscx, \fscy and \frz, one Line a frame, fitted
// on past the first or last Line by `frames`.
std::expected<SeriesFits, ExtendFailure> checkExtend(const EditSession &session, const AegisubFrameTimes &times,
                                                     int frames, ExtendAt at, const LineVisible &visible = {});
// checkExtend's fit, then that Line grows to cover the new frames and has the fit applied as motion
// data. One step, "Extend tracked lines".
std::expected<SeriesFits, ExtendFailure> extendTrackedLines(EditSession &session, const AegisubFrameTimes &times,
                                                            int frames, ExtendAt at, bool origin,
                                                            const LineVisible &visible = {});

// The first six-argument \move carried on at the same speed to the whole Line: \move(x1,y1,x2,y2)
// to two decimals. nullopt without one, or when its times are equal.
std::optional<std::u8string> extrapolateMove(std::u8string_view text, std::int64_t durationMs);
// One step, "Extrapolate \move"; the number of Lines changed.
std::expected<int, CommandRefusal> extrapolateMoves(EditSession &session, const LineVisible &visible = {});

}
