#pragma once

#include "hikari/application/aegisub_frame_times.h"
#include "hikari/application/align_to_video.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/motion_data.h"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

struct FramePoint {
    int x = 0, y = 0;
    bool operator==(const FramePoint &) const = default;
};

// The frames a Line shows, as frame_from_ms gives them: [first, first + count).
struct LineFrames {
    std::int64_t first = 0, count = 0;
};
LineFrames lineFrames(const AegisubFrameTimes &times, std::int64_t startMs, std::int64_t endMs);

// Where the picked point is on each frame. With a track, it moves by the track's offset from the
// row of `reference`; the track's row 0 is frame `trackFirst`, and frames outside it have no point.
class PointPath {
public:
    explicit PointPath(FramePoint picked) : m_picked(picked) {}
    PointPath(FramePoint picked, MotionTrack track, std::int64_t trackFirst, std::int64_t reference);
    std::optional<FramePoint> at(std::int64_t frame) const;
    bool tracked() const { return m_track.has_value(); }

private:
    FramePoint m_picked;
    std::optional<MotionTrack> m_track;
    std::int64_t m_trackFirst = 0, m_reference = 0;
};

// "&HBBGGRR&", as getPixelFormatted writes it.
std::u8string assColour(KeyColour colour);

struct ColourTrackOptions {
    bool alpha = false;
    std::array<bool, 4> colours{true, false, false, false}; // \c, \2c, \3c, \4c
    bool allAlpha = true;                                   // \alpha instead of `alphas`
    std::array<bool, 4> alphas{true, false, false, false};  // \1a .. \4a
    KeyColour sign{}, background{255, 255, 255};
    bool skipRepeats = true;
};

// zah.aegi-color-track's alpha between the sign and the background by perceived brightness, as
// two hex digits.
std::u8string trackedAlpha(KeyColour sign, KeyColour background, KeyColour current);

// The first frame's values as plain tags, then one \t(t,t,…) for each later frame at its offset
// from the first; a frame without a colour writes nothing. `offsetsMs` matches `colours`.
std::u8string colourTrackTags(std::span<const std::optional<KeyColour>> colours,
                              std::span<const std::int64_t> offsetsMs, const ColourTrackOptions &options);

// The tags the options write are removed from the first override block, and `tags` close it; a
// Line without one gets a new block.
std::u8string withColourTrack(std::u8string_view text, std::u8string_view tags, const ColourTrackOptions &options);

double colourDistance(KeyColour a, KeyColour b);

struct FadeOptions {
    bool in = true, out = true;
    double tolerance = 5;
};
enum class FadeProblem { InNotFound, OutNotFound };
struct FadeTimes {
    std::optional<std::int64_t> inMs, outMs;
    std::optional<std::int64_t> inFrame, outFrame; // the frames the fade ends and starts on
};
// phos.AutoFade's rules over `distances`, one for each frame from `first` (no value where the
// point could not be read). Fade in ends on the first frame from `first` up to `current` within
// tolerance; fade out starts on the first frame after `current` beyond it. A fade shorter than
// half a frame is 0.
std::expected<FadeTimes, FadeProblem> findFade(const AegisubFrameTimes &times,
                                               std::span<const std::optional<double>> distances,
                                               std::int64_t first, std::int64_t current, std::int64_t startMs,
                                               std::int64_t endMs, const FadeOptions &options);

// The point of a single-point vector clip, \clip(m x y) or \iclip(m x y).
std::optional<FramePoint> pointClip(std::u8string_view text);

// \fad(in,out) replaces the Line's \fad or opens its first block; an end not given keeps the
// Line's own, else 0. Nothing is written when both are 0. `dropClip` removes vector clips.
std::u8string withFade(std::u8string_view text, std::optional<std::int64_t> inMs, std::optional<std::int64_t> outMs,
                       bool dropClip);

// The texts in one step named `history`; Lines whose text is unchanged are left alone.
std::expected<void, CommandRefusal> replaceTexts(EditSession &session, const std::string &history,
                                                 const std::vector<std::pair<core::LineId, std::u8string>> &texts);

}
