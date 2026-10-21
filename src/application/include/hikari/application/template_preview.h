#pragma once

// KE2: the template editor's preview of the karaoke templater on one Line
// (docs/qt/karaoke-templater.md, "The template editor"). The templater runs
// on a reduced snapshot, the template and code Lines plus that Line, through
// "Apply karaoke template to selected lines"; its result is read, never
// applied. The video then draws the Document with that Line's generated Lines
// left out and the preview's put in, keeping only the Lines it can show.

#include "hikari/application/automation.h"
#include "hikari/core/document.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hikari::application {

// The Lines the templater reads, by its own rules (kara-templater.lua): a
// Comment whose Effect's first word is "template" or "code", in any case.
enum class TemplateKind { Template, Code };
std::optional<TemplateKind> templateKindOf(const core::LineRecord &line);
// A Line the templates apply to: a Dialogue with no Effect, or any Line
// whose Effect says karaoke (the original a run comments out).
bool isKaraokeLine(const core::LineRecord &line);
// A Line a run generated: a Dialogue whose Effect is fx.
bool isGeneratedLine(const core::LineRecord &line);

// The Line the preview runs on: the active Line when it is a karaoke Line,
// a generated Line's source, otherwise `last` while it is still a karaoke
// Line, otherwise the first karaoke Line; none without karaoke Lines.
std::optional<core::LineId> templatePreviewSource(const core::Document &doc, std::optional<core::LineId> active,
                                                  std::optional<core::LineId> last);

// The snapshot a preview runs on: the Script Info, the Styles, the template
// and code Lines and `source`, which is selected and active. `texts` gives
// template or code Lines text not yet in the Document. Nothing when `source`
// is not one of the Document's Lines.
std::optional<MacroSnapshot> templatePreviewSnapshot(const core::Document &doc, core::LineId source,
                                                     const std::map<core::LineId, std::u8string> &texts = {});

// The Lines the run generated from `source`, in order.
std::vector<core::LineRecord> templatePreviewLines(const MacroResult &result, core::LineId source);

// The Document Line a preview's report numbers `number` (1-based, as the
// report's template and issue entries do).
std::optional<core::LineId> templatePreviewLineOf(const MacroSnapshot &snapshot, int number);

// The span of time whose Lines the video's preview track keeps: the frame
// at `timeMs` when paused. While playing inside the preview's span
// [spanStart, spanEnd) it is that span, so a loop over the Line keeps one
// track; elsewhere it is every Line not over yet.
struct TemplatePreviewWindow {
    std::int64_t fromMs = 0, toMs = 0;
};
TemplatePreviewWindow templatePreviewWindow(std::int64_t timeMs, bool playing, std::int64_t spanStart,
                                            std::int64_t spanEnd);

// The Document the video draws: Lines generated from `source`, and the
// Lines in `replaced` (a file's unmarked generated Lines found to be its),
// left out; `preview` put in; only the Lines over `window` kept.
core::Document templatePreviewDocument(const core::Document &doc, core::LineId source,
                                       const std::vector<core::LineRecord> &preview,
                                       const std::set<core::LineId> &replaced, TemplatePreviewWindow window);

} // namespace hikari::application
