#pragma once

// KE2: the template editor's preview of the karaoke templater on one Line
// (docs/karaoke-templater.md, "The template editor"). The templater runs
// on a reduced snapshot, the template and code Lines plus that Line, through
// "Apply karaoke template to selected lines"; its result is read, never
// applied. The video then draws the Document with that Line's generated Lines
// left out and the preview's put in, keeping only the Lines it can show.

#include "hikari/application/automation.h"
#include "hikari/core/document.h"

#include <cstdint>
#include <functional>
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

// A file's generated Lines carry no marks (KE1 keeps them in memory only).
// Which karaoke Line each came from is found as KE1's selected-Lines run
// finds it: a run on the snapshot below gives every karaoke Line's Lines,
// and each karaoke Line, in file order, takes for each of its Lines the
// first unmarked generated Line still free that reads back the same.
// The snapshot: the Document without its generated Lines, every karaoke
// Line selected; nothing without karaoke Lines.
std::optional<MacroSnapshot> templateOwnersSnapshot(const core::Document &doc);
// The karaoke Line of each unmarked generated Line, from that run's result;
// none when the file holds only some of the Lines the templates give a
// karaoke Line now (one it holds none of takes none).
std::map<core::LineId, core::LineId> templateOwners(const core::Document &doc, const MacroResult &result);

// The snapshot a preview runs on: the Script Info, the Styles, the template
// and code Lines and `source`, which is selected and active; every other Line
// before `source` but the generated ones (a whole run deletes those first) is
// a templatePlaceholder, so `source` keeps its script index. `texts` gives
// template or code Lines text not yet in the Document. Nothing when `source`
// is not one of the Document's Lines.
std::optional<MacroSnapshot> templatePreviewSnapshot(const core::Document &doc, core::LineId source,
                                                     const std::map<core::LineId, std::u8string> &texts = {});

// An empty Comment of no Line, which the templater passes over: it holds a
// left-out Line's place so the Lines after it keep their script indices.
MacroDialogueLine templatePlaceholder();

// The Lines the run generated from `source`, in order.
std::vector<core::LineRecord> templatePreviewLines(const MacroResult &result, core::LineId source);

// The Document Line a preview's report numbers `number` (1-based, as the
// report's template and issue entries do).
std::optional<core::LineId> templatePreviewLineOf(const MacroSnapshot &snapshot, int number);

// The span of time whose Lines the video's preview track keeps: the frame
// at `timeMs` when paused. While playing inside the preview's span
// [spanStart, spanEnd) it is that span, so a loop over the Line keeps one
// track; elsewhere it is every Line not over yet, from the last of `ends`
// (the drawn Lines' ends, ascending) passed, so it changes only when a
// Line ends, not every frame.
struct TemplatePreviewWindow {
    std::int64_t fromMs = 0, toMs = 0;
};
TemplatePreviewWindow templatePreviewWindow(std::int64_t timeMs, bool playing, std::int64_t spanStart,
                                            std::int64_t spanEnd, const std::vector<std::int64_t> &ends);

// The Styles a run made or changed (karaskel's `-furigana` Style): the
// result's Styles the snapshot does not hold as they are.
std::vector<MacroStyleLine> templatePreviewStyles(const MacroSnapshot &snapshot, const MacroResult &result);

// The Document the video draws: Lines generated from `source` (still `fx`
// ones: Apply keeps a marked Line edited out of them), and the Lines in
// `replaced` (a file's unmarked generated Lines found to be its), left out,
// and the source itself when `preview` has Lines (Apply comments it out
// then); `preview` put in; only the Lines over `window` kept. `owners` gives
// the karaoke Line of each unmarked generated Line when it is known
// (templateOwners), which places a source's first Lines. `styles`
// (templatePreviewStyles) replace the file's of their name or follow them,
// as Apply puts them in.
core::Document templatePreviewDocument(const core::Document &doc, core::LineId source,
                                       const std::vector<core::LineRecord> &preview,
                                       const std::set<core::LineId> &replaced, TemplatePreviewWindow window,
                                       const std::map<core::LineId, core::LineId> &owners = {},
                                       const std::vector<MacroStyleLine> &styles = {});

// The host services a preview run may use: the ones that only read (text
// extents, frame times, the video's size, the project's properties).
// Templates run on every edit, so a preview never writes the clipboard, the
// status bar or the editor, and never opens a picker or a prompt.
bool isPreviewService(HostService service);

// The karaoke templater as the template editor uses it: previews in a
// helper of its own, and Apply through the automation shell.
class KaraokeTemplaterPort {
public:
    virtual ~KaraokeTemplaterPort() = default;
    // How a preview run ended: the result when it ran to the end, otherwise
    // what stopped it.
    struct Preview {
        std::optional<MacroResult> result;
        std::string error;
    };
    using PreviewDone = std::function<void(Preview)>;

    // A templater script is loaded, so previews and Apply can run.
    virtual bool available() const = 0;
    // Runs "Apply karaoke template to selected lines" on `snapshot` without
    // applying it. A newer preview replaces one not finished, whose `done`
    // is never called.
    virtual void preview(const MacroSnapshot &snapshot, PreviewDone done) = 0;
    virtual void cancelPreview() = 0;
    // Runs the stock macro on the editing target as the Automation menu does
    // (every karaoke Line, or with `selectedOnly` the selected Lines); false
    // when it cannot start.
    virtual bool apply(bool selectedOnly) = 0;
};

} // namespace hikari::application
