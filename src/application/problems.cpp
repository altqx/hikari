#include "hikari/application/problems.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <charconv>

namespace hikari::application {

namespace {

constexpr std::string_view kIdPrefix = "p";

std::vector<QcNote> openNotes(const core::Document &document)
{
    std::vector<QcNote> notes = documentQcNotes(document);
    std::erase_if(notes, [](const QcNote &note) { return note.done; });
    return notes;
}

}

const std::u8string &problemFieldText(const core::LineRecord &line, ProblemField field)
{
    return field == ProblemField::Translation ? line.translation : line.text;
}

bool Problems::set(const std::string &owner, const std::string &source, const std::string &name, DocumentId document,
                   const std::string &set, std::vector<Problem> problems)
{
    if (held(owner, document, source, set) + problems.size() > kMostPerDocument)
        return false;
    const auto it = m_sources.find(source);
    const bool had = it != m_sources.end() && it->second.documents.contains(document.value) &&
                     it->second.documents.at(document.value).contains(set);
    if (problems.empty()) {
        if (had)
            clear(source, document, set);
        return true;
    }
    Source &into = m_sources[source];
    into.owner = owner;
    into.name = name;
    std::vector<HeldProblem> &list = into.documents[document.value][set];
    list.clear();
    list.reserve(problems.size());
    for (Problem &problem : problems)
        list.push_back(HeldProblem{std::string(kIdPrefix) + std::to_string(m_next++), source, set, std::move(problem)});
    changed(document, {source});
    return true;
}

std::size_t Problems::clear(const std::string &source, std::optional<DocumentId> document, std::optional<std::string> set)
{
    const auto it = m_sources.find(source);
    if (it == m_sources.end())
        return 0;
    std::size_t cleared = 0;
    std::vector<DocumentId> touched;
    auto &documents = it->second.documents;
    for (auto doc = documents.begin(); doc != documents.end();) {
        if (document && doc->first != document->value) {
            ++doc;
            continue;
        }
        std::size_t here = 0;
        for (auto s = doc->second.begin(); s != doc->second.end();) {
            if (set && s->first != *set) {
                ++s;
                continue;
            }
            here += s->second.size();
            s = doc->second.erase(s);
        }
        if (here)
            touched.push_back(DocumentId{doc->first});
        cleared += here;
        doc = doc->second.empty() ? documents.erase(doc) : std::next(doc);
    }
    if (documents.empty())
        m_sources.erase(it);
    for (const DocumentId d : touched)
        changed(d, {source});
    return cleared;
}

void Problems::clearOwner(const std::string &owner)
{
    std::map<std::uint64_t, std::vector<std::string>> touched;
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if (it->second.owner != owner) {
            ++it;
            continue;
        }
        for (const auto &[document, sets] : it->second.documents)
            touched[document].push_back(it->first);
        it = m_sources.erase(it);
    }
    for (auto &[document, sources] : touched)
        changed(DocumentId{document}, std::move(sources));
}

void Problems::keepOnly(const std::vector<DocumentId> &open)
{
    const auto closed = [&](std::uint64_t document) { return std::ranges::find(open, DocumentId{document}) == open.end(); };
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        std::erase_if(it->second.documents, [&](const auto &entry) { return closed(entry.first); });
        it = it->second.documents.empty() ? m_sources.erase(it) : std::next(it);
    }
    std::erase_if(m_openNotes, [&](const auto &entry) { return closed(entry.first); });
}

void Problems::refresh(DocumentId id, const core::Document &document)
{
    std::vector<std::string> sources;
    absl::flat_hash_map<std::uint64_t, const core::LineRecord *> lines;
    const auto lineOf = [&](core::LineId line) -> const core::LineRecord * {
        if (lines.empty())
            for (const core::LineRecord *record : document.lines())
                lines.emplace(record->id.value, record);
        const auto found = lines.find(line.value);
        return found == lines.end() ? nullptr : found->second;
    };
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        const auto doc = it->second.documents.find(id.value);
        if (doc == it->second.documents.end()) {
            ++it;
            continue;
        }
        bool dropped = false;
        for (auto s = doc->second.begin(); s != doc->second.end();) {
            dropped |= std::erase_if(s->second, [&](const HeldProblem &held) {
                           const core::LineRecord *line = lineOf(held.problem.line);
                           return !line || problemFieldText(*line, held.problem.field) != held.problem.against;
                       }) > 0;
            s = s->second.empty() ? doc->second.erase(s) : std::next(s);
        }
        if (dropped)
            sources.push_back(it->first);
        if (doc->second.empty())
            it->second.documents.erase(doc);
        it = it->second.documents.empty() ? m_sources.erase(it) : std::next(it);
    }
    std::vector<QcNote> notes = openNotes(document);
    if (std::vector<QcNote> &seen = m_openNotes[id.value]; seen != notes) {
        seen = std::move(notes);
        sources.emplace_back(kQcSource);
    }
    if (!sources.empty())
        changed(id, std::move(sources));
}

std::vector<HeldProblem> Problems::of(DocumentId document) const
{
    std::vector<HeldProblem> out;
    for (const auto &[source, held] : m_sources)
        if (const auto doc = held.documents.find(document.value); doc != held.documents.end())
            for (const auto &[set, problems] : doc->second)
                out.insert(out.end(), problems.begin(), problems.end());
    return out;
}

const HeldProblem *Problems::find(DocumentId document, std::string_view id) const
{
    for (const auto &[source, held] : m_sources)
        if (const auto doc = held.documents.find(document.value); doc != held.documents.end())
            for (const auto &[set, problems] : doc->second)
                for (const HeldProblem &problem : problems)
                    if (problem.id == id)
                        return &problem;
    return nullptr;
}

bool Problems::gave(std::string_view id) const
{
    if (!id.starts_with(kIdPrefix) || id.size() == kIdPrefix.size())
        return false;
    std::uint64_t serial = 0;
    const char *end = id.data() + id.size();
    const auto [at, error] = std::from_chars(id.data() + kIdPrefix.size(), end, serial);
    return error == std::errc() && at == end && serial > 0 && serial < m_next;
}

std::string Problems::name(const std::string &source) const
{
    const auto it = m_sources.find(source);
    return it == m_sources.end() ? std::string() : it->second.name;
}

std::size_t Problems::held(const std::string &owner, DocumentId document, const std::string &source,
                           const std::string &set) const
{
    std::size_t count = 0;
    for (const auto &[id, held] : m_sources)
        if (held.owner == owner)
            if (const auto doc = held.documents.find(document.value); doc != held.documents.end())
                for (const auto &[name, problems] : doc->second)
                    if (id != source || name != set)
                        count += problems.size();
    return count;
}

void Problems::changed(DocumentId document, std::vector<std::string> sources) const
{
    if (m_changed)
        m_changed(document, std::move(sources));
}

}
