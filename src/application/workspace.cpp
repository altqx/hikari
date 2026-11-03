#include "hikari/application/workspace.h"

#include <algorithm>

namespace hikari::application {

DocumentId Workspace::add(std::string title)
{
    const DocumentId id{m_next++};
    m_documents.push_back(Entry{id, std::move(title), m_activeWindow});
    if (!m_target)
        m_target = id;
    return id;
}

bool Workspace::add(DocumentId id, std::string title, bool asReference)
{
    if (find(id))
        return false;
    m_documents.push_back(Entry{id, std::move(title), m_activeWindow});
    m_next = std::max(m_next, id.value + 1);
    if (asReference) {
        m_reference = id;
        m_referenceIsTab = false;
    } else if (!m_target)
        m_target = id;
    return true;
}

bool Workspace::remove(DocumentId id)
{
    const auto it = std::ranges::find(m_documents, id, &Entry::id);
    if (it == m_documents.end())
        return false;
    const int window = it->window;
    const auto before = tabs(window);
    const auto position = static_cast<std::size_t>(std::ranges::find(before, id) - before.begin());
    m_documents.erase(it);
    if (m_reference == id)
        m_reference.reset();
    const auto taking = [&]() -> std::optional<DocumentId> {
        const auto after = tabs(window);
        if (after.empty())
            return std::nullopt;
        return after[std::min(position, after.size() - 1)];
    };
    if (m_target == id) {
        m_target = taking();
        if (m_target && m_target == m_reference)
            m_reference.reset();
    } else if (const auto current = m_inactiveCurrent.find(window);
               current != m_inactiveCurrent.end() && current->second == id) {
        if (const auto next = taking())
            current->second = *next;
        else
            m_inactiveCurrent.erase(current);
    }
    return true;
}

bool Workspace::replace(DocumentId id, DocumentId replacement)
{
    const auto it = std::ranges::find(m_documents, id, &Entry::id);
    const auto with = std::ranges::find(m_documents, replacement, &Entry::id);
    if (it == m_documents.end() || with == m_documents.end() || id == replacement)
        return false;
    Entry entry = *with;
    entry.window = it->window;
    m_documents.erase(with);
    *std::ranges::find(m_documents, id, &Entry::id) = std::move(entry);
    if (m_target == id)
        m_target = replacement;
    for (auto &[window, current] : m_inactiveCurrent)
        if (current == id)
            current = replacement;
    if (m_reference == id)
        m_reference = replacement;
    return true;
}

std::vector<DocumentId> Workspace::tabs(int window) const
{
    std::vector<DocumentId> out;
    for (const auto &e : m_documents)
        if (e.window == window && (e.id != m_reference || m_referenceIsTab))
            out.push_back(e.id);
    return out;
}

std::vector<DocumentId> Workspace::allTabs() const
{
    std::vector<DocumentId> out;
    for (const auto &e : m_documents)
        if (e.id != m_reference || m_referenceIsTab)
            out.push_back(e.id);
    return out;
}

std::optional<DocumentId> Workspace::currentTab(int window) const
{
    if (window == m_activeWindow)
        return m_target;
    const auto it = m_inactiveCurrent.find(window);
    return it == m_inactiveCurrent.end() ? std::nullopt : std::optional(it->second);
}

void Workspace::leaveWindow()
{
    if (m_target)
        m_inactiveCurrent[m_activeWindow] = *m_target;
    else
        m_inactiveCurrent.erase(m_activeWindow);
}

std::optional<DocumentId> Workspace::takeCurrent(int window)
{
    const auto it = m_inactiveCurrent.find(window);
    if (it == m_inactiveCurrent.end())
        return std::nullopt;
    const DocumentId current = it->second;
    m_inactiveCurrent.erase(it);
    return current;
}

bool Workspace::activateWindow(int window)
{
    if (window == m_activeWindow)
        return false;
    leaveWindow();
    m_activeWindow = window;
    m_target = takeCurrent(window);
    if (m_target && m_target == m_reference)
        m_reference.reset();
    return true;
}

std::vector<DocumentId> Workspace::removeWindow(int window)
{
    std::vector<DocumentId> removed;
    for (const auto &e : m_documents)
        if (e.window == window)
            removed.push_back(e.id);
    std::erase_if(m_documents, [&](const Entry &e) { return e.window == window; });
    if (m_reference && std::ranges::contains(removed, *m_reference))
        m_reference.reset();
    m_inactiveCurrent.erase(window);
    if (window == m_activeWindow) {
        m_activeWindow = 0;
        m_target = takeCurrent(0);
    }
    return removed;
}

bool Workspace::swapTabs(std::size_t a, std::size_t b)
{
    const auto order = tabs();
    if (a >= order.size() || b >= order.size())
        return false;
    if (a == b)
        return true;
    const auto first = std::ranges::find(m_documents, order[a], &Entry::id);
    const auto second = std::ranges::find(m_documents, order[b], &Entry::id);
    std::iter_swap(first, second);
    return true;
}

const Workspace::Entry *Workspace::find(DocumentId id) const
{
    const auto it = std::ranges::find(m_documents, id, &Entry::id);
    return it == m_documents.end() ? nullptr : &*it;
}

Workspace::Entry *Workspace::find(DocumentId id)
{
    const auto it = std::ranges::find(m_documents, id, &Entry::id);
    return it == m_documents.end() ? nullptr : &*it;
}

const std::string *Workspace::title(DocumentId id) const
{
    const Entry *entry = find(id);
    return entry ? &entry->title : nullptr;
}

bool Workspace::setEditingTarget(DocumentId id)
{
    const Entry *entry = find(id);
    if (!entry || (m_reference == id && !m_referenceIsTab))
        return false;
    if (m_reference == id)
        m_reference.reset();
    if (entry->window != m_activeWindow) {
        leaveWindow();
        m_activeWindow = entry->window;
        m_inactiveCurrent.erase(m_activeWindow);
    }
    m_target = id;
    return true;
}

bool Workspace::setReference(std::optional<DocumentId> id)
{
    if (id && (!find(*id) || m_target == id))
        return false;
    m_reference = id;
    m_referenceIsTab = false;
    return true;
}

bool Workspace::setTabReference(DocumentId id)
{
    if (!find(id) || m_target == id)
        return false;
    m_reference = id;
    m_referenceIsTab = true;
    return true;
}

bool Workspace::promoteReference()
{
    if (!m_reference)
        return false;
    find(*m_reference)->window = m_activeWindow;
    m_target = m_reference;
    m_reference.reset();
    return true;
}

std::expected<void, TargetRefusal> Workspace::checkContentCommand(DocumentId target) const
{
    if (!find(target))
        return std::unexpected(TargetRefusal::NoDocument);
    if (m_reference == target)
        return std::unexpected(TargetRefusal::ProtectedReference);
    if (m_target != target)
        return std::unexpected(TargetRefusal::NotEditingTarget);
    return {};
}

std::optional<int> Workspace::window(DocumentId id) const
{
    const Entry *entry = find(id);
    return entry ? std::optional(entry->window) : std::nullopt;
}

bool Workspace::setTitle(DocumentId id, std::string title)
{
    for (auto &e : m_documents)
        if (e.id == id) {
            e.title = std::move(title);
            return true;
        }
    return false;
}

std::vector<DocumentId> Workspace::documents() const
{
    std::vector<DocumentId> out;
    for (const auto &e : m_documents)
        out.push_back(e.id);
    return out;
}

}
