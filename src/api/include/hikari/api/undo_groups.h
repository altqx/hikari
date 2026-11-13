#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/workspace.h"

#include <optional>
#include <string>
#include <vector>

namespace hikari::api {

class DocumentHost;

// document.group_begin/end: the steps one caller commits on one Document while
// its group is open become one History step.
class UndoGroups {
public:
    enum class Begin { Opened, AlreadyOpen, OtherCaller };

    Begin begin(const std::string &caller, application::DocumentId document, std::string name,
                application::ContentId now);
    // How many steps became one; nullopt when the caller has no open group.
    std::optional<std::size_t> end(const std::string &caller, DocumentHost &documents);
    // After each write by `caller`: its steps so far belong to the group.
    void wrote(const std::string &caller, DocumentHost &documents);
    // A commit, Undo or Redo on `document`. `writer`: the caller whose write is running, if any.
    // Anything but that group's own write ends the group at its last step.
    void changed(application::DocumentId document, const std::string *writer, DocumentHost &documents);

private:
    struct Group {
        std::string caller;
        application::DocumentId document;
        std::string name;
        application::ContentId base, last;
    };
    std::size_t close(std::vector<Group>::iterator group, DocumentHost &documents);

    std::vector<Group> m_groups;
};

}
