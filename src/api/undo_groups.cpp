#include "hikari/api/undo_groups.h"

#include "hikari/api/host.h"

#include <algorithm>

namespace hikari::api {

using application::ContentId;
using application::DocumentId;

namespace {

std::optional<std::size_t> stepOf(const application::EditSession &session, ContentId content)
{
    const auto steps = session.history();
    const auto it = std::ranges::find(steps, content, &application::EditSession::HistoryStep::content);
    return it == steps.end() ? std::nullopt : std::optional(std::size_t(it - steps.begin()));
}

}

UndoGroups::Begin UndoGroups::begin(const std::string &caller, DocumentId document, std::string name, ContentId now)
{
    for (const Group &g : m_groups) {
        if (g.caller == caller)
            return Begin::AlreadyOpen;
        if (g.document == document)
            return Begin::OtherCaller;
    }
    m_groups.push_back(Group{caller, document, std::move(name), now, now});
    return Begin::Opened;
}

std::optional<std::size_t> UndoGroups::end(const std::string &caller, DocumentHost &documents)
{
    const auto it = std::ranges::find(m_groups, caller, &Group::caller);
    if (it == m_groups.end())
        return std::nullopt;
    return close(it, documents);
}

void UndoGroups::wrote(const std::string &caller, DocumentHost &documents)
{
    const auto it = std::ranges::find(m_groups, caller, &Group::caller);
    if (it == m_groups.end())
        return;
    if (const application::EditSession *s = documents.session(it->document))
        it->last = s->contentId();
}

void UndoGroups::changed(DocumentId document, const std::string *writer, DocumentHost &documents)
{
    const auto it = std::ranges::find(m_groups, document, &Group::document);
    if (it == m_groups.end() || (writer && *writer == it->caller))
        return;
    const application::EditSession *s = documents.session(document);
    if (s && s->contentId() == it->last)
        return;
    close(it, documents);
}

std::size_t UndoGroups::close(std::vector<Group>::iterator group, DocumentHost &documents)
{
    const Group g = std::move(*group);
    m_groups.erase(group);
    application::EditSession *s = documents.session(g.document);
    if (!s)
        return 0;
    const auto from = stepOf(*s, g.base);
    const auto to = stepOf(*s, g.last);
    if (!from || !to || *to <= *from || !s->foldSteps(g.base, g.last, g.name))
        return 0;
    documents.committed(g.document);
    return *to - *from;
}

}
