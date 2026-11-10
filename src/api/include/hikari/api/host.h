#pragma once

// Ports the composition root implements; every call comes on the GUI thread.

#include "hikari/application/ai_review.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/settings.h"
#include "hikari/application/subtitle_render.h"
#include "hikari/application/workspace.h"

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

// Draws subtitles as the video does, in a render context apart from the video's.
class RenderHost {
public:
    virtual ~RenderHost() = default;
    // `document`'s Lines at `frame`'s start, laid out and drawn at width x height with `id`'s fonts;
    // nullopt without video or when the renderer fails.
    virtual std::optional<application::OverlayFrame> render(application::DocumentId id, const core::Document &document,
                                                            std::int64_t frame, int width, int height) = 0;
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
};

}
