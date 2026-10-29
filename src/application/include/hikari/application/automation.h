#pragma once

#include <cfloat>
#include <cstdint>
#include <functional>
#include <optional>
#include <climits>
#include <string>
#include <variant>
#include <vector>

namespace hikari::application {

struct ScriptMacro {
    std::string name;
    std::string description;
    bool hasValidate = false;
    bool hasIsActive = false;
};

struct ScriptInfo {
    std::string name; // script_name, or the file name when the script sets none
    std::string description;
    std::string author;
    std::string version;
    std::vector<ScriptMacro> macros;
    // Files the top level loaded as Lua code, transitively, first load first,
    // each once, as absolute normalized UTF-8 paths; excludes the script itself.
    std::vector<std::string> loadedFiles;
};

// One control. `kind` is the lowercased Lua class: label, edit, textbox,
// intedit, floatedit, dropdown, checkbox, color, coloralpha or alpha.
struct DialogControl {
    std::string kind;
    std::string name;
    std::string hint;
    int x = 0, y = 0, width = 1, height = 1;
    std::string label; // label and checkbox
    std::string text;  // edit, textbox, alpha, dropdown and colour values
    int intValue = 0, intMin = INT_MIN, intMax = INT_MAX;                // intedit
    double number = 0, numberMin = -DBL_MAX, numberMax = DBL_MAX, step = 0; // floatedit
    bool checked = false;            // checkbox
    std::vector<std::string> items;  // dropdown
};

struct DialogRequest {
    std::vector<DialogControl> controls;
    // Button labels in order. Empty: the default pair, OK then Cancel.
    std::vector<std::string> buttons;
    // One per button, from the script's third argument: ok, yes, save, apply,
    // close, no, cancel, help or context_help; empty for none.
    std::vector<std::string> buttonIds;
};

enum class DialogValueType { None, Text, Integer, Number, Boolean };

inline DialogValueType dialogValueType(const std::string &kind)
{
    if (kind == "label")
        return DialogValueType::None;
    if (kind == "intedit")
        return DialogValueType::Integer;
    if (kind == "floatedit")
        return DialogValueType::Number;
    if (kind == "checkbox")
        return DialogValueType::Boolean;
    return DialogValueType::Text;
}

using DialogValue = std::variant<std::monostate, std::string, int, double, bool>;

// A closed dialog still returns every value.
struct DialogResult {
    int pressed = -1;                // button index; -1 when the dialog was closed
    std::vector<DialogValue> values; // one per control, in request order
};

inline DialogResult initialDialogResult(const DialogRequest &request)
{
    DialogResult result;
    for (const DialogControl &c : request.controls) {
        switch (dialogValueType(c.kind)) {
        case DialogValueType::None: result.values.emplace_back(); break;
        case DialogValueType::Text: result.values.emplace_back(c.text); break;
        case DialogValueType::Integer: result.values.emplace_back(c.intValue); break;
        case DialogValueType::Number: result.values.emplace_back(c.number); break;
        case DialogValueType::Boolean: result.values.emplace_back(c.checked); break;
        }
    }
    return result;
}

// Script indices are 1-based across info, then styles, then dialogue lines.
struct MacroInfoLine {
    std::string key, value;
    bool operator==(const MacroInfoLine &) const = default;
};
struct MacroStyleLine {
    std::vector<std::string> fields; // positional ASS v4+ fields, Name first
    bool operator==(const MacroStyleLine &) const = default;
};
struct MacroDialogueLine {
    std::uint64_t id = 0; // the Line it came from; 0 for a line the macro added
    // Line a karaoke templater generated this one from, 0 for none; never saved.
    std::uint64_t generatedFrom = 0;
    bool comment = false;
    int layer = 0;
    std::int64_t startMs = 0, endMs = 0;
    std::string style, actor;
    int marginL = 0, marginR = 0, marginV = 0;
    std::string effect;
    std::string text;        // as the script sees it (the translation in TLMode)
    std::string translation; // "text_translation": the original in TLMode, else empty
    std::string raw;         // the line as ASS text (read-only)
    bool operator==(const MacroDialogueLine &) const = default;
};

struct MacroSnapshot {
    std::uint64_t revision = 0; // the Document revision the macro started from
    std::vector<MacroInfoLine> info;
    std::vector<MacroStyleLine> styles;
    std::vector<MacroDialogueLine> dialogues;
    std::vector<int> selected; // script indices
    int active = 0;            // script index of the active Line, 0 for none
    bool canModify = true;
};

struct MacroResult {
    std::vector<MacroInfoLine> info;
    std::vector<MacroStyleLine> styles;
    std::vector<MacroDialogueLine> dialogues;
    std::optional<std::vector<int>> selected; // nullopt: the macro returned none
    std::optional<int> active;
    // False when validation answered false or raised (validationError): the
    // macro did not run, the lists are empty and applyMacroResult refuses it.
    bool valid = true;
    std::string validationError;
    // What the script attached with aegisub.hikari.report; empty for none.
    std::string report;
};

// Answered asynchronously, once. Per service (positions are 0-based UTF-16 offsets in the Line editor):
//   FrameFromMs, MsFromFrame  integers {ms | frame} -> integers {frame | ms}
//   VideoSize                 -> integers {width, height, arX, arY}
//   Keyframes                 -> integers (frame numbers)
//   Frame                     integers {frame, withSubtitles} -> integers {width, height}, pixels (BGRA rows)
//   AudioSelection            -> integers {startMs, endMs}
//   ProjectProperties         -> integers {video frame shown}, strings {audio, video, keyframes file}
//   TextExtents               style, strings {text} -> numbers {width, height, descent, external leading}
//   ClipboardGet              -> strings {text}
//   ClipboardSet              strings {text} -> integers {succeeded}
//   OpenFiles                 strings {title, dir, file, wildcard}, integers {multiple, mustExist} -> strings (paths)
//   SaveFile                  strings {title, dir, file, wildcard}, integers {promptOverwrite} -> strings {path}
//   StatusText                strings {text}
//   DecodePath                strings {path} -> strings {path}
//   FileName                  -> strings {subtitle file name}
//   EditorCursor              -> integers {position};      SetEditorCursor     integers {position}
//   EditorSelection           -> integers {start, end};    SetEditorSelection  integers {start, end}
//   EditorModified            -> integers {modified}
//   FrequencyPeaks            integers {start, end, freqStart, freqEnd, peek} -> integers {status, times...},
//                             numbers {intensities} (status 1: an audio box without audio yet; Unavailable: none)
//   Gettext                   strings {source} -> strings {text} (Unavailable or no string: the source)
//   ScriptResolution          integers {PlayResX, PlayResY as staged, 0 missing} -> integers {width, height}
//                             (asked only while one is missing; the answer goes into the staged
//                             Script Info; Unavailable: nothing written)
// Unavailable reaches the script as nil: no video, no Document, a cancelled picker.
enum class HostService : std::int32_t {
    FrameFromMs = 1, MsFromFrame, VideoSize, Keyframes, Frame, AudioSelection, ProjectProperties, TextExtents,
    ClipboardGet, ClipboardSet, OpenFiles, SaveFile, StatusText, DecodePath, FileName,
    EditorCursor, SetEditorCursor, EditorSelection, SetEditorSelection, EditorModified,
    FrequencyPeaks, Gettext, ScriptResolution,
};
inline constexpr std::int32_t kLastHostService = static_cast<std::int32_t>(HostService::ScriptResolution);

struct HostServiceRequest {
    HostService service = HostService::FrameFromMs;
    // Set by the application, never taken from the helper.
    std::string script;
    std::uint64_t run = 0;
    std::vector<std::int64_t> integers;
    std::vector<std::string> strings;
    std::vector<std::string> style; // TextExtents: the style's positional fields, Name first
};

struct HostServiceReply {
    enum class Status : std::int32_t { Ok = 0, Unavailable = 1 };
    Status status = Status::Ok;
    std::vector<std::int64_t> integers;
    std::vector<double> numbers;
    std::vector<std::string> strings;
    std::vector<std::byte> pixels;
    static HostServiceReply unavailable() { return HostServiceReply{Status::Unavailable, {}, {}, {}, {}}; }
};

struct ScriptStatus {
    enum class State { Loading, Ready, Running, LoadFailed, Unavailable };
    std::string path;
    State state = State::Loading;
    ScriptInfo info;
    std::string error;
    std::uint64_t generation = 0; // a reload or restart is a new helper generation
    bool forceStopOffered = false; // cancelled, and still running after the grace period
};

class AutomationServicePort {
public:
    virtual ~AutomationServicePort() = default;
    virtual std::vector<ScriptStatus> scripts() const = 0;
    virtual void load(const std::string &path) = 0;   // runs the script's top level in a new helper
    virtual bool reload(const std::string &path) = 0; // top level runs again
    virtual void unload(const std::string &path) = 0;
    virtual bool run(const std::string &path, int ordinal) = 0; // false while any macro runs
    virtual void cancel() = 0;
    // Only after it was offered: kills that script's helper.
    virtual bool forceStop(const std::string &path) = 0;
    virtual void setObserver(std::function<void()> changed) = 0;
};

}
