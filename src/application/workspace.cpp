#include "hikari/application/workspace.h"

#include <algorithm>

namespace hikari::application {

DocumentId Workspace::add(std::string title)
{
    const DocumentId id{m_next++};
    m_documents.push_back(Entry{id, std::move(title)});
    if (!m_target)
        m_target = id;
    return id;
}

bool Workspace::add(DocumentId id, std::string title, bool asReference)
{
    if (find(id))
        return false;
    m_documents.push_back(Entry{id, std::move(title)});
    m_next = std::max(m_next, id.value + 1);
    if (asReference)
        m_reference = id;
    else if (!m_target)
        m_target = id;
    return true;
}

bool Workspace::remove(DocumentId id)
{
    const auto it = std::ranges::find(m_documents, id, &Entry::id);
    if (it == m_documents.end())
        return false;
    const auto before = tabs();
    const auto position = static_cast<std::size_t>(std::ranges::find(before, id) - before.begin());
    m_documents.erase(it);
    if (m_reference == id)
        m_reference.reset();
    if (m_target == id) {
        // Legacy DeletePage: the tab now at the closed one's index, else the last.
        m_target.reset();
        if (const auto after = tabs(); !after.empty())
            m_target = after[std::min(position, after.size() - 1)];
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
    m_documents.erase(with);
    *std::ranges::find(m_documents, id, &Entry::id) = std::move(entry);
    if (m_target == id)
        m_target = replacement;
    if (m_reference == id)
        m_reference = replacement;
    return true;
}

std::vector<DocumentId> Workspace::tabs() const
{
    std::vector<DocumentId> out;
    for (const auto &e : m_documents)
        if (e.id != m_reference)
            out.push_back(e.id);
    return out;
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

const std::string *Workspace::title(DocumentId id) const
{
    const Entry *entry = find(id);
    return entry ? &entry->title : nullptr;
}

bool Workspace::setEditingTarget(DocumentId id)
{
    if (!find(id) || m_reference == id)
        return false;
    m_target = id;
    return true;
}

bool Workspace::setReference(std::optional<DocumentId> id)
{
    if (id && (!find(*id) || m_target == id))
        return false;
    m_reference = id;
    return true;
}

bool Workspace::promoteReference()
{
    if (!m_reference)
        return false;
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

} // namespace hikari::application
