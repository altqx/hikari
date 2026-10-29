#pragma once

// Keyboard focus never moves either target; only the explicit operations below do.

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
    // As the protected reference it never becomes the editing target.
    bool add(DocumentId id, std::string title, bool asReference = false);
    // The editing target passes to the tab taking its place (the next tab, else the last), never
    // to the reference.
    bool remove(DocumentId id);
    // `replacement` (already added) takes `id`'s tab position and roles, and `id` goes.
    bool replace(DocumentId id, DocumentId replacement);
    // Every Document but the protected reference, unless it is a tab reference.
    std::vector<DocumentId> tabs() const;
    // `a` and `b` index tabs(); the roles stay with their Documents.
    bool swapTabs(std::size_t a, std::size_t b);

    std::optional<DocumentId> editingTarget() const { return m_target; }
    std::optional<DocumentId> reference() const { return m_reference; }
    const std::string *title(DocumentId id) const;
    bool setTitle(DocumentId id, std::string title);
    std::vector<DocumentId> documents() const; // in the order they were added

    // The reference can't become the editing target this way (use promoteReference); a Document
    // can't be both. Choosing a tab reference's tab makes it the editing target, no longer the reference.
    bool setEditingTarget(DocumentId id);
    bool setReference(std::optional<DocumentId> id);
    // Another tab's Document becomes the protected reference and stays a tab; a reference that was
    // not a tab becomes one.
    bool setTabReference(DocumentId id);
    bool referenceIsTab() const { return m_reference && m_referenceIsTab; }
    // The reference stops being protected; the previous target stays open with its content and context.
    bool promoteReference();

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
    bool m_referenceIsTab = false;
    std::uint64_t m_next = 1;
};

}
