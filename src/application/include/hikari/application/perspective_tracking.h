#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/application/visual_perspective.h"

#include <expected>
#include <optional>
#include <string_view>
#include <vector>

namespace hikari::application {

// After Effects 6.0 keyframe data with the four CC Power Pin corners, one quad a frame.
struct PowerPin {
    std::vector<visual::perspective::Quad> frames;
    double fps = 0;
    int width = 0, height = 0; // the size it was tracked at; 0 when not given
};
// nullopt unless every corner is there with as many frames as the others.
std::optional<PowerPin> parsePowerPin(std::u8string_view text);

struct TrackingOptions {
    int relativeFrame = 1; // 1 .. the data's frames
    visual::perspective::OrgMode orgMode = visual::perspective::OrgMode::Keep;
    bool fitFlat = false;
    bool moveClips = true;
    bool scaleBorders = true;
    int videoWidth = 0, videoHeight = 0;
    visual::perspective::TextMeasure measure;
};

// The selected shown Lines' frames: [first, first + count).
struct TrackedFrames {
    int first = 0, count = 0;
};
std::optional<TrackedFrames> trackedFrames(const EditSession &session, const LegacyTimebase &timebase,
                                           const LineVisible &visible = {});

enum class TrackingProblem { NoLines, Length, NoLineAtFrame, Overlap };
struct TrackingCheck {
    std::optional<TrackingProblem> problem;
    TrackedFrames frames;
    int row = 0; // NoLineAtFrame and Overlap: a Line not shown at the relative frame
};
TrackingCheck checkTracking(const EditSession &session, const LegacyTimebase &timebase, const PowerPin &data,
                            int relativeFrame, const LineVisible &visible = {});

// Each selected shown Line becomes one Line a frame, as splitIntoFrames bakes them, with that frame's
// quad followed from the relative frame's and saved in its extradata (perspective::kPlaneKey). One step,
// "Perspective tracking"; the new Lines are selected.
std::expected<void, CommandRefusal> applyTracking(EditSession &session, const LegacyTimebase &timebase,
                                                  const PowerPin &data, const TrackingOptions &options,
                                                  const LineVisible &visible = {});

}
