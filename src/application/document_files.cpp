#include "hikari/application/document_files.h"

#include "hikari/core/subtitle_load.h"

#include <algorithm>

namespace hikari::application {

namespace {

// Lower-case extension of a path, without the dot.
std::u8string extensionOf(const std::string &path)
{
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::u8string ext;
    for (char c : path.substr(dot + 1))
        ext += static_cast<char8_t>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return ext;
}

OpenError openError(ReadError e)
{
    switch (e) {
    case ReadError::NotFound: return OpenError::NotFound;
    case ReadError::AccessDenied: return OpenError::AccessDenied;
    case ReadError::Failed: break;
    }
    return OpenError::ReadFailed;
}

} // namespace

DocumentFiles::DocumentFiles(FileReadPort &reader, WriteCoordinator &writes) : m_reader(reader), m_writes(writes) {}

DocumentFiles::Entry *DocumentFiles::find(DocumentId document)
{
    const auto it = m_documents.find(document);
    return it == m_documents.end() ? nullptr : &it->second;
}

std::expected<StagedOpen, OpenError> DocumentFiles::stage(const DestinationKey &destination)
{
    auto bytes = m_reader.read(destination);
    if (!bytes)
        return std::unexpected(openError(bytes.error()));
    auto load = core::loadSubtitle(*bytes, extensionOf(destination.value));
    if (!load)
        return std::unexpected(OpenError::InvalidFormat);
    return StagedOpen{destination, std::move(*load), std::nullopt, 0, 0};
}

std::expected<StagedOpen, OpenError> DocumentFiles::stageOpen(const DestinationKey &destination)
{
    return stage(destination);
}

std::expected<StagedOpen, OpenError> DocumentFiles::stageReload(DocumentId document)
{
    Entry *entry = find(document);
    if (!entry)
        return std::unexpected(OpenError::UnknownDocument);
    auto staged = stage(entry->destination);
    if (staged) {
        staged->replaces = document;
        staged->expectedGeneration = entry->generation;
        staged->expectedRevision = entry->session->revision();
    }
    return staged;
}

std::expected<DocumentId, OpenError> DocumentFiles::activate(StagedOpen staged)
{
    std::vector<std::byte> bytes = staged.load.document.source().bytes;
    if (staged.replaces) {
        Entry *entry = find(*staged.replaces);
        if (!entry)
            return std::unexpected(OpenError::UnknownDocument);
        if (entry->generation != staged.expectedGeneration || entry->session->revision() != staged.expectedRevision)
            return std::unexpected(OpenError::StaleTarget);
        // A reload starts a new lifetime generation with fresh history.
        entry->session = std::make_unique<EditSession>(std::move(staged.load.document));
        entry->session->markSaved(entry->session->contentId());
        entry->knownBytes = std::move(bytes);
        ++entry->generation;
        entry->lastSave.reset();
        return *staged.replaces;
    }
    const DocumentId id{m_nextDocument++};
    Entry entry;
    entry.session = std::make_unique<EditSession>(std::move(staged.load.document));
    entry.session->markSaved(entry.session->contentId());
    entry.destination = staged.destination;
    entry.knownBytes = std::move(bytes);
    m_documents.emplace(id, std::move(entry));
    m_writes.associate(id, staged.destination);
    return id;
}

EditSession *DocumentFiles::session(DocumentId document)
{
    Entry *entry = find(document);
    return entry ? entry->session.get() : nullptr;
}

std::optional<DestinationKey> DocumentFiles::destination(DocumentId document) const
{
    const auto it = m_documents.find(document);
    return it == m_documents.end() ? std::nullopt : std::optional(it->second.destination);
}

std::uint64_t DocumentFiles::generation(DocumentId document) const
{
    const auto it = m_documents.find(document);
    return it == m_documents.end() ? 0 : it->second.generation;
}

bool DocumentFiles::close(DocumentId document)
{
    if (!m_documents.erase(document))
        return false;
    m_writes.dissociate(document);
    return true;
}

std::expected<SavePlan, SaveRefusal> DocumentFiles::prepareSave(DocumentId document,
                                                                std::optional<DestinationKey> saveAs)
{
    Entry *entry = find(document);
    if (!entry)
        return std::unexpected(SaveRefusal::UnknownDocument);
    const DestinationKey destination = saveAs ? *saveAs : entry->destination;
    if (destination.value.empty())
        return std::unexpected(SaveRefusal::NoDestination);
    auto snapshot = entry->session->prepareSave(); // commit-then-save
    return SavePlan{document,      entry->generation,
                    snapshot.revision, snapshot.content,
                    destination,   core::encodeSubtitle(snapshot.document),
                    saveAs.has_value() && *saveAs != entry->destination};
}

std::expected<PermitId, SaveRefusal> DocumentFiles::startSave(SavePlan plan, bool overwriteExternalChange)
{
    Entry *entry = find(plan.document);
    if (!entry)
        return std::unexpected(SaveRefusal::UnknownDocument);
    if (entry->generation != plan.generation || entry->session->revision() != plan.revision)
        return std::unexpected(SaveRefusal::StalePlan);
    if (!plan.saveAs && !overwriteExternalChange) {
        // Overwriting our own file: it must still hold what we last saw there.
        const auto current = m_reader.read(plan.destination);
        if (!current || *current != entry->knownBytes)
            return std::unexpected(SaveRefusal::ExternalChange);
    }
    auto permit = m_writes.requestPermit(plan.document, plan.destination, plan.revision);
    if (!permit)
        return std::unexpected(permit.error() == PermitRefusal::DestinationOwnedByOtherDocument
                                   ? SaveRefusal::Collision
                                   : SaveRefusal::WriteInProgress);
    m_pending.emplace(*permit, Pending{plan.document, plan.generation, plan.content, plan.destination, plan.bytes,
                                       plan.saveAs});
    entry->lastSave = SaveStatus{*permit, std::nullopt};
    if (!m_writes.write(*permit, std::move(plan.bytes))) {
        m_pending.erase(*permit);
        entry->lastSave.reset();
        return std::unexpected(SaveRefusal::WriteInProgress);
    }
    return *permit;
}

void DocumentFiles::onWriteResult(const WriteResult &result)
{
    const auto it = m_pending.find(result.permit);
    if (it == m_pending.end())
        return;
    Pending pending = std::move(it->second);
    m_pending.erase(it);
    Entry *entry = find(pending.document);
    // A result for a closed or reloaded Document is late: it can't mark the
    // current content saved or move its association.
    if (!entry || entry->generation != pending.generation)
        return;
    if (entry->lastSave && entry->lastSave->permit == result.permit)
        entry->lastSave->outcome = result.outcome;
    const bool published =
        result.outcome == WriteOutcome::Written || result.outcome == WriteOutcome::DurabilityUncertain;
    if (!published)
        return; // failed or cancelled: the work stays unsaved, nothing moves
    // Only the written snapshot becomes saved; newer edits stay unsaved.
    entry->session->markSaved(pending.content);
    entry->knownBytes = std::move(pending.bytes);
    if (pending.saveAs) {
        entry->destination = pending.destination;
        m_writes.associate(pending.document, pending.destination);
    }
}

std::optional<SaveStatus> DocumentFiles::lastSave(DocumentId document) const
{
    const auto it = m_documents.find(document);
    return it == m_documents.end() ? std::nullopt : it->second.lastSave;
}

} // namespace hikari::application
