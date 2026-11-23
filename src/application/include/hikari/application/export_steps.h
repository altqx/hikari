#pragma once

#include "hikari/application/aegisub_frame_times.h"
#include "hikari/application/automation.h"
#include "hikari/core/document.h"

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Export as's Before export steps: Aegisub's export filters, run on a copy of the Document.

inline constexpr char kKaraokeTemplateStep[] = "karaoke-template";
inline constexpr char kRemoveTagsStep[] = "remove-tags";
inline constexpr char kTransformFrameRateStep[] = "transform-frame-rate";
inline constexpr char kFixStylesStep[] = "fix-styles";
inline constexpr std::u8string_view kExportFiltersKey = u8"Export Filters";

// A step's settings by control name, as a Lua filter's config dialog returns them.
using ExportStepSettings = std::map<std::string, DialogValue>;

struct ExportStep {
    std::string id; // a built-in's id, or luaStepId()
    bool on = true;
    ExportStepSettings settings;
    bool operator==(const ExportStep &) const = default;
};

// Steps, their order and their settings; not the format.
struct ExportRecipe {
    std::vector<ExportStep> steps;
    bool operator==(const ExportRecipe &) const = default;
};

// A step Export as can offer.
struct AvailableStep {
    std::string id;
    std::string name;        // as HikariSub shows it
    std::string aegisubName; // as Aegisub names the filter in a file's Export Filters line
    int priority = 0;
    // The script file whose filter runs the step: a Lua step's, and Apply karaoke template's templater; empty
    // when nothing provides it.
    std::string script;
    bool hasConfig = false;
};

std::string luaStepId(const std::string &scriptFile, const std::string &filterName);
// The script file and filter name of a luaStepId(); nullopt for a built-in's id.
std::optional<std::pair<std::string, std::string>> luaStepParts(const std::string &id);

// The four built-ins, then each Ready script's filters; a filter named as a built-in's Aegisub filter is that
// built-in. In Aegisub's order: by priority, highest first, the built-ins first among equals.
std::vector<AvailableStep> availableSteps(const std::vector<ScriptStatus> &scripts);
const AvailableStep *findStep(const std::vector<AvailableStep> &available, const std::string &id);

// Every available step, off, with its default settings.
ExportRecipe newStepList(const std::vector<AvailableStep> &available);
ExportStepSettings defaultSettings(const std::string &id);

// An Export Filters line's names, '|' separated.
std::vector<std::string> exportFilterNames(std::string_view line);
// The Document's [Aegisub Project Garbage] Export Filters names.
std::vector<std::string> exportFilterNames(const core::Document &document);
// "From this file (Aegisub)": the named filters in the line's order, on. A name no available step has stays as a
// Lua step whose script isn't loaded.
ExportRecipe recipeFromExportFilters(const std::vector<std::string> &names, const std::vector<AvailableStep> &available);
// The steps that are on, in order, by their Aegisub names.
std::string exportFiltersLine(const ExportRecipe &recipe, const std::vector<AvailableStep> &available);

// Aegisub's Fix Styles: a Line whose style the Document doesn't have, compared without ASCII case, gets
// "Default". Returns the Lines changed.
int fixUnknownStyles(core::Document &document);

// Aegisub's Transform Framerate: every Line's times, and the \t, \move, \fad, \fade and karaoke times in its
// override blocks, go from the input frame rate's frames to the output's.
struct FrameRateChange {
    AegisubFrameTimes input, output;
};
int transformFrameRate(core::Document &document, const FrameRateChange &change);
// A constant frame rate's frame times, as Aegisub's agi::vfr::Framerate(double) holds them.
std::optional<AegisubFrameTimes> constantFrameTimes(double fps);

// A Lua filter's settings as the dialog the helper reads them from.
DialogRequest settingsDialog(const ExportStepSettings &settings);

// Runs Lua export filters for Export as.
class ExportFilterPort {
public:
    virtual ~ExportFilterPort() = default;
    using Done = std::function<void(std::expected<MacroResult, std::string>)>;
    // The script is loaded, trusted and has the filter.
    virtual bool available(const std::string &scriptFile, const std::string &filterName) const = 0;
    // Runs the filter on the snapshot; `done` gets its result or what stopped it. False when it can't start.
    virtual bool run(const std::string &scriptFile, const std::string &filterName, const MacroSnapshot &snapshot,
                     const DialogRequest &settings, Done done) = 0;
};

struct ExportStepOutcome {
    // Skipped: its script isn't loaded or trusted.
    enum class State { Done, Failed, NotRun, Skipped };
    // Busy: a macro or another step was running, so the script couldn't start.
    enum class Problem { None, ScriptError, NotApplied, NoTimecodes, BadFrameRate, Busy };
    std::string id, name;
    State state = State::NotRun;
    int lines = 0; // Done: the Lines changed; for Apply karaoke template, the Lines made
    Problem problem = Problem::None;
    std::string error; // ScriptError: the script's message; empty when it gave none
    bool operator==(const ExportStepOutcome &) const = default;
};

struct ExportStepsContext {
    // The loaded video's or timecodes' frame times; Transform frame rate's Variable output needs them.
    std::optional<AegisubFrameTimes> timecodes;
    ExportFilterPort *filters = nullptr;
};

struct ExportStepsResult {
    core::Document document;
    std::vector<ExportStepOutcome> steps; // one per step that is on, in order
    bool failed = false;                 // a step failed: nothing may be written
};

// Runs the recipe's steps that are on, in order, on `document`. A step whose script isn't available is skipped;
// a failing step stops the run and the rest don't run. `done` may be called before this returns.
void runExportSteps(core::Document document, const ExportRecipe &recipe, const std::vector<AvailableStep> &available,
                    const ExportStepsContext &context, std::function<void(ExportStepsResult)> done);

}
