#pragma once

// Y4: resampling (legacy GLOBAL_OPEN_SUBS_RESAMPLE with SubsResampleDialog,
// the SubsMismatchResolutionDialog and SubsGrid::ResizeSubs / GetASSRes at
// 20d647c4).

#include "hikari/application/edit_session.h"

#include <expected>
#include <functional>
#include <string>

namespace hikari::application {

struct Resolution {
    int width = 0, height = 0;
    bool operator==(const Resolution &) const = default;
};

// SubsGrid::GetASSRes: PlayResX/PlayResY, with 1280x720 when both are
// missing and a 16:9 partner for one missing value (as int).
Resolution scriptResolution(const core::Document &document);

// A value that could not be scaled: its 1-based row, the value and its tag
// (legacy "In line %i, value '%s' cannot be scaled\nin tag '%s'").
using ResampleWarning = std::function<void(int row, const std::u16string &value, const std::u16string &tag)>;

// SubsGrid::ResizeSubs on a Document: Style margins, font size, outline,
// shadow and spacing (and ScaleX when stretching); each non-comment Line's
// margins and its \pos \move \org \clip \iclip (rectangles and drawings)
// \bord \xbord \ybord \shad \xshad \yshad \fs \fsp \fscx tags and drawings
// (\p). The text changed is the translation when there is one.
void resizeSubtitles(core::Document &document, float xScale, float yScale, bool stretch,
                     const ResampleWarning &warn = {});

// The legacy dialogs' OK ("Changing subtitles resolution"): PlayResX/Y
// become `to` (LayoutResX/Y too when LayoutResX is set), then the Document
// is resized from `from` unless `resample` is false. Refused for a
// Document that is not ASS.
std::expected<void, CommandRefusal> changeResolution(EditSession &session, Resolution from, Resolution to,
                                                     bool resample, bool stretch, const ResampleWarning &warn = {});

} // namespace hikari::application
