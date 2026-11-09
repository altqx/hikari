#pragma once

// Keyboard focus never moves either target; only the explicit operations below do.

#include "hikari/application/write_coordinator.h"

#include <expected>
#include <map>
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
    // Adds a Document to the active window; the window's first one becomes the editing target.
    DocumentId add(std::string title);
    // As the protected reference it never becomes the editing target.
    bool add(DocumentId id, std::string title, bool asReference = false);
    // The window's current tab passes to the tab taking its place (the next tab, else the last), never
    // to the reference.
    bool remove(DocumentId id);
    // `replacement` (already added) takes `id`'s tab position and roles, and `id` goes.
    bool replace(DocumentId id, DocumentId replacement);
    // Every Document but the protected reference, unless it is a tab reference, in the active window.
    std::vector<DocumentId> tabs() const { return tabs(m_activeWindow); }
    std::vector<DocumentId> tabs(int window) const;
    std::vector<DocumentId> allTabs() const;
    // `a` and `b` index tabs(); the roles stay with their Documents.
    bool swapTabs(std::size_t a, std::size_t b);
    // The tab moves to `window` at `index` of that window's tabs (or last) and becomes the editing target there,
    // as if chosen. The window it left passes its current tab on as remove() does.
    bool moveTab(DocumentId id, int window, std::size_t index);

    // The editing target is the current tab of the active window.
    std::optional<DocumentId> editingTarget() const { return m_target; }
    std::optional<DocumentId> currentTab(int window) const;
    int activeWindow() const { return m_activeWindow; }
    // False when the window was already active.
    bool activateWindow(int window);
    // The window's Documents leave the Workspace; the first window takes over when it was active.
    std::vector<DocumentId> removeWindow(int window);
    std::optional<DocumentId> reference() const { return m_reference; }
    const std::string *title(DocumentId id) const;
    std::optional<int> window(DocumentId id) const;
    bool setTitle(DocumentId id, std::string title);
    std::vector<DocumentId> documents() const; // in the order they were added

    // The reference can't become the editing target this way (use promoteReference); a Document
    // can't be both. Choosing a tab reference's tab makes it the editing target, no longer the reference.
    // The Document's window becomes the active window.
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
        int window = 0;
    };
    const Entry *find(DocumentId id) const;
    Entry *find(DocumentId id);
    void leaveWindow();
    std::optional<DocumentId> takeCurrent(int window);

    std::vector<Entry> m_documents;
    std::optional<DocumentId> m_target;
    int m_activeWindow = 0;
    std::map<int, DocumentId> m_inactiveCurrent;
    std::optional<DocumentId> m_reference;
    bool m_referenceIsTab = false;
    std::uint64_t m_next = 1;
};

}
