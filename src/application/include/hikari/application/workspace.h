#pragma once

// The shared workspace's document targets (docs/qt/ux/workspaces.md). The
// editing target receives content-changing commands. An optional protected
// comparison reference can be read, selected and copied, never changed.
// Keyboard focus is a UI concern and never moves either target; only the
// explicit operations below do.

#include "hikari/application/write_coordinator.h"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

enum class TargetRefusal {
    NoDocument,         // nothing is open, or the Document is unknown
    ProtectedReference, // the comparison reference cannot be changed
    NotEditingTarget,   // general editing commands go to the editing target only
};

class Workspace {
public:
    // Adds a Document; the first one becomes the editing target.
    DocumentId add(std::string title);
    // Adds a Document that already has an identity (from DocumentFiles). As
    // the protected reference it never becomes the editing target.
    bool add(DocumentId id, std::string title, bool asReference = false);
    // Removes a Document. Removing the editing target hands it to the first
    // remaining unprotected Document, if any.
    bool remove(DocumentId id);

    std::optional<DocumentId> editingTarget() const { return m_target; }
    std::optional<DocumentId> reference() const { return m_reference; }
    const std::string *title(DocumentId id) const;
    bool setTitle(DocumentId id, std::string title); // after Save As
    std::vector<DocumentId> documents() const;    // in the order they were added

    // Explicit operations. The reference can't become the editing target this
    // way (use promoteReference); a Document can't be both.
    bool setEditingTarget(DocumentId id);
    bool setReference(std::optional<DocumentId> id);
    // Makes the reference the editing target. It stops being protected; the
    // previous target stays open with its content and context.
    bool promoteReference();

    // Guard for a content-changing command aimed at `target`.
    std::expected<void, TargetRefusal> checkContentCommand(DocumentId target) const;

private:
    struct Entry {
        DocumentId id;
        std::string title;
    };
    const Entry *find(DocumentId id) const;

    std::vector<Entry> m_documents;
    std::optional<DocumentId> m_target;
    std::optional<DocumentId> m_reference;
    std::uint64_t m_next = 1;
};

} // namespace hikari::application
