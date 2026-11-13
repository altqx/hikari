#include "hikari/application/row_marks.h"

#include <algorithm>

namespace hikari::application {

bool RowMarks::set(const std::string &owner, const std::string &label, DocumentId document, const std::vector<RowMark> &marks)
{
    const std::map<std::uint64_t, RowMark> *held = nullptr;
    if (const auto it = m_owners.find(owner); it != m_owners.end())
        if (const auto doc = it->second.documents.find(document.value); doc != it->second.documents.end())
            held = &doc->second;
    std::size_t total = held ? held->size() : 0;
    for (const RowMark &mark : marks)
        if (!held || !held->contains(mark.line.value))
            ++total;
    if (total > kMostPerDocument)
        return false;
    if (marks.empty())
        return true;
    Owned &into = m_owners[owner];
    into.label = label;
    auto &lines = into.documents[document.value];
    for (const RowMark &mark : marks)
        lines[mark.line.value] = mark;
    changed(document);
    return true;
}

void RowMarks::clear(const std::string &owner, DocumentId document, const std::vector<core::LineId> &lines)
{
    const auto it = m_owners.find(owner);
    if (it == m_owners.end())
        return;
    const auto doc = it->second.documents.find(document.value);
    if (doc == it->second.documents.end())
        return;
    if (lines.empty())
        doc->second.clear();
    for (const core::LineId line : lines)
        doc->second.erase(line.value);
    if (doc->second.empty())
        it->second.documents.erase(doc);
    if (it->second.documents.empty())
        m_owners.erase(it);
    changed(document);
}

void RowMarks::clearOwner(const std::string &owner)
{
    const auto it = m_owners.find(owner);
    if (it == m_owners.end())
        return;
    std::vector<DocumentId> touched;
    for (const auto &[document, lines] : it->second.documents)
        touched.push_back(DocumentId{document});
    m_owners.erase(it);
    for (const DocumentId document : touched)
        changed(document);
}

void RowMarks::keepOnly(const std::vector<DocumentId> &open)
{
    for (auto it = m_owners.begin(); it != m_owners.end();) {
        std::erase_if(it->second.documents, [&](const auto &entry) {
            return std::ranges::find(open, DocumentId{entry.first}) == open.end();
        });
        it = it->second.documents.empty() ? m_owners.erase(it) : std::next(it);
    }
}

std::vector<RowMark> RowMarks::of(const std::string &owner, DocumentId document) const
{
    std::vector<RowMark> out;
    if (const auto it = m_owners.find(owner); it != m_owners.end())
        if (const auto doc = it->second.documents.find(document.value); doc != it->second.documents.end())
            for (const auto &[line, mark] : doc->second)
                out.push_back(mark);
    return out;
}

std::map<std::uint64_t, std::vector<OwnedMark>> RowMarks::shown(DocumentId document) const
{
    std::map<std::uint64_t, std::vector<OwnedMark>> out;
    for (const auto &[owner, owned] : m_owners)
        if (const auto doc = owned.documents.find(document.value); doc != owned.documents.end())
            for (const auto &[line, mark] : doc->second)
                out[line].push_back(OwnedMark{owner, owned.label, mark.colour, mark.tooltip});
    return out;
}

std::map<std::uint64_t, std::size_t> RowMarks::counts(const std::string &owner) const
{
    std::map<std::uint64_t, std::size_t> out;
    if (const auto it = m_owners.find(owner); it != m_owners.end())
        for (const auto &[document, lines] : it->second.documents)
            out[document] = lines.size();
    return out;
}

void RowMarks::changed(DocumentId document) const
{
    if (m_changed)
        m_changed(document);
}

}
