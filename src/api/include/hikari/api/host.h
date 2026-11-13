#pragma once

// Ports the composition root implements; every call comes on the GUI thread.

#include "hikari/application/ai_review.h"
#include "hikari/application/audio_karaoke.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/dialog_memory.h"
#include "hikari/application/row_marks.h"
#include "hikari/application/furigana.h"
#include "hikari/application/karaoke_split.h"
#include "hikari/application/template_preview.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/node_effects.h"
#include "hikari/application/settings.h"
#include "hikari/application/subtitle_render.h"
#include "hikari/application/vcs.h"
#include "hikari/application/visual_perspective.h"
#include "hikari/application/workspace.h"

#include <filesystem>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::api {

class DocumentHost {
public:
    virtual ~DocumentHost() = default;
    virtual const application::Workspace &workspace() const = 0;
    // Null for a Document the Workspace does not know.
    virtual application::EditSession *session(application::DocumentId document) const = 0;
    // nullopt: never saved.
    virtual std::optional<std::string> path(application::DocumentId document) const = 0;
    // As the Grid would: the views follow and selectionChanged is reported. False
    // when the Document cannot take it.
    virtual bool select(application::DocumentId document, const application::Selection &selection) = 0;
    // The API committed a step on `document`; the views refresh and
    // documentChanged is reported.
    virtual void committed(application::DocumentId document) = 0;
};

struct CommandState {
    bool enabled = true;
    std::string disabledReason;
    std::vector<std::string> keys; // accelerator text, "Ctrl-Shift-O"
};

class CommandHost {
public:
    virtual ~CommandHost() = default;
    // nullopt: the shell has no handler for the command.
    virtual std::optional<CommandState> state(std::string_view command) const = 0;
    // Runs an enabled command as its menu item would; false when it did not run.
    virtual bool run(std::string_view command) = 0;
};

struct PanelState {
    std::string id; // the workspace layout's panel id, e.g. "Grid"
    std::string title;
    bool open = false;
    bool floating = false;
    bool focused = false;
};

class PanelHost {
public:
    virtual ~PanelHost() = default;
    virtual std::vector<PanelState> panels() const = 0;
    // False for an unknown panel.
    virtual bool setOpen(std::string_view panel, bool open) = 0;
    virtual bool focus(std::string_view panel) = 0;
};

// Reads and writes the files a caller was granted, by path.
class FileHost {
public:
    virtual ~FileHost() = default;
    // At most `atMost` bytes from the start; nullopt: the file cannot be read.
    virtual std::optional<std::string> read(const std::string &path, std::size_t atMost) const = 0;
    // Replaces the file whole; false when it could not.
    virtual bool write(const std::string &path, std::string_view bytes) = 0;
};

enum class SkillOrigin { Project, Mine };

struct SkillFolder {
    std::string name;
    std::vector<std::string> files; // relative to the folder, '/'-separated, SKILL.md among them
    SkillOrigin origin = SkillOrigin::Mine;
};

// The skill folders a caller sees (docs/ai-skills.md). `project` is the caller's Project folder, empty for none;
// its skills hide the user's of the same name.
class SkillHost {
public:
    virtual ~SkillHost() = default;
    virtual std::vector<SkillFolder> skills(const std::filesystem::path &project) const = 0;
    // At most `atMost` bytes of a file listed under `skill`; nullopt: it cannot be read.
    virtual std::optional<std::string> read(const std::filesystem::path &project, std::string_view skill,
                                            std::string_view file, std::size_t atMost) const = 0;
};

// Where a Document's Honya project folder is: the glossary, characters and style its translation shares with Honya.
class TranslationHost {
public:
    virtual ~TranslationHost() = default;
    // nullopt: the Document has no project folder.
    virtual std::optional<std::string> projectFolder(application::DocumentId document) const = 0;
};

// Draws subtitles as the video does, in a render context apart from the video's.
class RenderHost {
public:
    virtual ~RenderHost() = default;
    // `document`'s Lines at `frame`'s start, laid out and drawn at width x height with `id`'s fonts;
    // nullopt without video or when the renderer fails.
    virtual std::optional<application::OverlayFrame> render(application::DocumentId id, const core::Document &document,
                                                            std::int64_t frame, int width, int height) = 0;
};

struct LuaScript {
    std::string name; // the file's stem in the AI scripts folder
    std::string text;
    std::string macro; // empty: the script's first macro
};

struct LuaRun {
    std::string outcome; // "applied", "refused", "failed", "cancelled", "declined", "stopped", "lost" or "invalid"
    std::string message;
    std::string log;
    std::string file;
};

enum class LuaStart { Started, Busy, NoDocument, NotSaved };

// Runs AI-written Lua the user approved as an ordinary Automation macro on the editing target.
class AutomationHost {
public:
    virtual ~AutomationHost() = default;
    // `done` runs once, later, only when this answers Started.
    virtual LuaStart run(const LuaScript &script, std::function<void(LuaRun)> done) = 0;
};

struct MacroInfo {
    std::string script; // the script's name
    std::string file;   // its file name, as aliases know it
    std::string macro;
    std::string description;
    int ordinal = 0;
    bool enabled = true; // loaded, and shown in the user's window
    std::string disabledReason;
};

// One dialog's answer: its ScriptDialogMemory key, the values by control name, the button's label.
struct DialogAnswer {
    std::string key;
    application::NamedDialogValues values;
    std::string button;
};

struct MacroRequest {
    std::string file, macro;
    std::vector<DialogAnswer> answers;
    bool showAnswered = false;
};

enum class MacroStart { Started, Busy, NoDocument, NotFound, Ambiguous, Disabled };

// The user's loaded Automation macros, run as the menu runs them.
class MacroHost {
public:
    virtual ~MacroHost() = default;
    virtual std::vector<MacroInfo> macros() const = 0;
    // `done` runs once, later, only when this answers Started.
    virtual MacroStart runMacro(const MacroRequest &request, std::function<void(LuaRun)> done) = 0;
};

// The editing target's video keyframes.
class KeyframeHost {
public:
    virtual ~KeyframeHost() = default;
    // Replaces the keyframes as Open keyframes does; with `file`, writes them there first and opens that file.
    // Answers why it could not.
    virtual std::optional<std::string> setKeyframes(const std::vector<std::int64_t> &frames,
                                                    const std::optional<std::string> &file) = 0;
};

// What the typesetting solvers measure text with.
class TypesetHost {
public:
    virtual ~TypesetHost() = default;
    // A measure without a port: text Lines cannot be measured, drawings still can.
    virtual application::visual::perspective::TextMeasure measure() const = 0;
};

struct ProjectState {
    std::string name;
    std::string root;
    bool trusted = false;
};

// The Project of the window the user works in (ADR 0030).
class ProjectHost {
public:
    virtual ~ProjectHost() = default;
    // nullopt: that window has no Project.
    virtual std::optional<ProjectState> project() const = 0;
    // ai.exclude as the Project at `root` sets it; empty for a folder that is no Project.
    virtual std::vector<std::string> aiExclude(const std::string &root) const = 0;
};

// An external aligner's job: the audio from startMs to endMs and the words sung there.
struct AlignRequest {
    std::string program; // "whisperx" or "mfa"
    std::int64_t startMs = 0, endMs = 0;
    std::string text;
    std::string language;                   // WhisperX's --language; empty: it detects
    std::string dictionary, acousticModel; // MFA's
};

struct AlignedWord {
    std::string text;
    std::int64_t startMs = 0, endMs = 0;
};

struct AlignResult {
    std::string outcome; // "aligned", "missing" (not installed), "no_audio" or "failed"
    std::string message;
    std::vector<AlignedWord> words;
};

// What the karaoke tools need from the app.
class KaraokeHost {
public:
    virtual ~KaraokeHost() = default;
    virtual application::KaraokeCharClass classes() const = 0;
    // Null: English words stay one syllable.
    virtual const application::Hyphenation *hyphenation() const = 0;
    // Empty: no reading dictionary is installed.
    virtual application::ReadingAnalyzer readings() const = 0;
    // The open audio's vocal onsets in ms, ascending, or nullopt without audio; `done` runs once.
    virtual void onsets(std::function<void(std::optional<std::vector<int>>)> done) = 0;
    // Null: no templater script is loaded. Its `done` always runs, once.
    virtual application::KaraokeTemplaterPort *templater() = 0;
    // Runs the user's installed aligner; `done` runs once, later.
    virtual void align(const AlignRequest &request, std::function<void(AlignResult)> done) = 0;
};

struct PluginCommand {
    std::string command; // "plugin.<plugin id>.<command id>"
    std::string title;
};

struct PluginState {
    std::string id, name, version, description;
    std::string source; // "file" or "marketplace"
    bool signedPackage = false, enabled = false, fullTrust = false, revoked = false;
    std::vector<PluginCommand> commands; // only while it runs
};

struct PluginFile {
    std::string path; // '/'-separated, relative to the plugin's folder
    std::string text;
};

struct ScaffoldedPlugin {
    std::string folder, package;
};

enum class PluginRun { Ran, NotInstalled, NotRunning, UnknownCommand };

// What the install question shows for a package.
struct PluginReview {
    std::string id, name, version;
    std::string change; // "install", "update", "reinstall" or "downgrade"
    std::string installedVersion;
    bool fullTrust = false, needsRetrust = false;
    std::string keyId, pinnedKeyId; // empty: unsigned, or nothing pinned yet
    std::vector<std::string> permissions; // as the install question words them
    std::vector<std::string> commands;    // the titles of the commands it adds
    std::vector<PluginFile> files; // the package's text files, main first
    std::string digest;            // the package's SHA-256, hex
};

// The user's installed plugins, and the AI plugins folder agents scaffold into.
class PluginHost {
public:
    virtual ~PluginHost() = default;
    virtual std::vector<PluginState> plugins() const = 0;
    // The installed hikari-plugin.json; nullopt when the plugin isn't installed.
    virtual std::optional<std::string> manifest(std::string_view plugin) const = 0;
    virtual PluginRun run(std::string_view plugin, std::string_view command) = 0;
    // Replaces the plugin's folder in the AI plugins folder with the files and packs them; or why it could not.
    virtual std::expected<ScaffoldedPlugin, std::string> scaffold(std::string_view plugin,
                                                                 const std::vector<PluginFile> &files) = 0;
    // The package a scaffold wrote; nullopt when there is none.
    virtual std::optional<std::string> scaffoldedPackage(std::string_view plugin) const = 0;
    virtual std::expected<PluginReview, std::string> review(const std::string &package) = 0;
    // Installs and turns on the package only while its SHA-256 is `digest`.
    virtual std::expected<PluginReview, std::string> install(const std::string &package, const std::string &digest) = 0;
};

// Runs node-effect graphs as the node-effect editor does, but never trusts a Document's Lua.
class EffectsHost {
public:
    virtual ~EffectsHost() = default;
    virtual application::NodeEffectOptions options() = 0;
};

// A missing port makes its operations answer unavailable.
struct Host {
    DocumentHost *documents = nullptr;
    CommandHost *commands = nullptr;
    PanelHost *panels = nullptr;
    application::Settings *settings = nullptr;
    application::MacroMediaPort *media = nullptr;
    FileHost *files = nullptr;
    application::AiReview *review = nullptr;
    RenderHost *render = nullptr;
    SkillHost *skills = nullptr;
    AutomationHost *automation = nullptr;
    TypesetHost *typeset = nullptr;
    ProjectHost *project = nullptr;
    TranslationHost *translation = nullptr;
    KaraokeHost *karaoke = nullptr;
    EffectsHost *effects = nullptr;
    PluginHost *plugins = nullptr;
    MacroHost *macros = nullptr;
    KeyframeHost *keyframes = nullptr;
    application::RowMarks *marks = nullptr;
    application::VcsPort *vcs = nullptr;
};

}
