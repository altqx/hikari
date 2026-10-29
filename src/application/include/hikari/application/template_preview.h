#pragma once

// The templater runs on a reduced snapshot; its result is read, never applied.

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

// As kara-templater.lua reads them: a Comment whose Effect's first word is
// "template" or "code", in any case.
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

// Generated Lines in a file carry no marks. The snapshot: the Document without
// its generated Lines, every karaoke Line selected; nullopt without karaoke Lines.
std::optional<MacroSnapshot> templateOwnersSnapshot(const core::Document &doc);
// From that run's result, each karaoke Line in file order takes the first free
// generated Line that reads back the same. Empty when the file holds only some
// of the Lines the templates now give a karaoke Line.
std::map<core::LineId, core::LineId> templateOwners(const core::Document &doc, const MacroResult &result);

// The Script Info, Styles, template and code Lines and `source` (selected and
// active); other Lines before `source` but generated ones become placeholders,
// so `source` keeps its script index. `texts`: template or code Line text not
// yet in the Document. nullopt when `source` is not in the Document.
std::optional<MacroSnapshot> templatePreviewSnapshot(const core::Document &doc, core::LineId source,
                                                     const std::map<core::LineId, std::u8string> &texts = {});

// An empty Comment the templater passes over, holding a left-out Line's place.
MacroDialogueLine templatePlaceholder();

// The Lines the run generated from `source`, in order.
std::vector<core::LineRecord> templatePreviewLines(const MacroResult &result, core::LineId source);

// The Document Line a preview's report numbers `number` (1-based, as the
// report's template and issue entries do).
std::optional<core::LineId> templatePreviewLineOf(const MacroSnapshot &snapshot, int number);

// Paused: the frame at `timeMs`. Playing inside [spanStart, spanEnd): that span;
// elsewhere every Line not over yet, from the last of `ends` (ascending) passed.
struct TemplatePreviewWindow {
    std::int64_t fromMs = 0, toMs = 0;
};
TemplatePreviewWindow templatePreviewWindow(std::int64_t timeMs, bool playing, std::int64_t spanStart,
                                            std::int64_t spanEnd, const std::vector<std::int64_t> &ends);

// The result's Styles the snapshot does not hold as they are (karaskel's
// `-furigana` Style).
std::vector<MacroStyleLine> templatePreviewStyles(const MacroSnapshot &snapshot, const MacroResult &result);

// Lines generated from `source`, the Lines in `replaced` and, when `preview` has
// Lines, `source` itself are left out; `preview` goes in; only Lines over
// `window` are kept. `owners` places a source's first Lines; `styles` replace
// the file's of their name or follow them.
core::Document templatePreviewDocument(const core::Document &doc, core::LineId source,
                                       const std::vector<core::LineRecord> &preview,
                                       const std::set<core::LineId> &replaced, TemplatePreviewWindow window,
                                       const std::map<core::LineId, core::LineId> &owners = {},
                                       const std::vector<MacroStyleLine> &styles = {});

// Only read-only services: a preview never writes the clipboard, the status bar
// or the editor, and never opens a picker or a prompt.
bool isPreviewService(HostService service);

// Previews run in a helper of their own; Apply goes through the automation shell.
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
