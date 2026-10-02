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
    m_documents.erase(it);
    if (m_reference == id)
        m_reference.reset();
    if (m_target == id) {
        m_target.reset();
        for (const auto &entry : m_documents)
            if (entry.id != m_reference) {
                m_target = entry.id;
                break;
            }
    }
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

} // namespace hikari::application
