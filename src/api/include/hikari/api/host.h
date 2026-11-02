#pragma once

// Ports the composition root implements; every call comes on the GUI thread.

#include "hikari/application/edit_session.h"
#include "hikari/application/settings.h"
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
};

// A missing port makes its operations answer unavailable.
struct Host {
    DocumentHost *documents = nullptr;
    CommandHost *commands = nullptr;
    PanelHost *panels = nullptr;
    application::Settings *settings = nullptr;
};

}
