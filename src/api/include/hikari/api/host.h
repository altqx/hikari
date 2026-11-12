#pragma once

// Ports the composition root implements; every call comes on the GUI thread.

#include "hikari/application/ai_review.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/settings.h"
#include "hikari/application/subtitle_render.h"
#include "hikari/application/visual_perspective.h"
#include "hikari/application/workspace.h"

#include <filesystem>
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
};

}
