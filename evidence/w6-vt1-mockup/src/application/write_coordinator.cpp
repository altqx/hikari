#include "hikari/application/write_coordinator.h"

namespace hikari::application {

WriteCoordinator::WriteCoordinator(FilePort &port, ResultListener onResult)
    : m_port(port), m_onResult(std::move(onResult))
{
}

void WriteCoordinator::associate(DocumentId document, const DestinationKey &destination)
{
    m_associations[document] = destination;
}

void WriteCoordinator::dissociate(DocumentId document)
{
    m_associations.erase(document);
}

std::optional<DestinationKey> WriteCoordinator::association(DocumentId document) const
{
    const auto it = m_associations.find(document);
    if (it == m_associations.end())
        return std::nullopt;
    return it->second;
}

std::optional<DocumentId> WriteCoordinator::ownerOf(const DestinationKey &destination) const
{
    for (const auto &[document, key] : m_associations)
        if (key == destination)
            return document;
    return std::nullopt;
}

bool WriteCoordinator::hasActivePermit(const DestinationKey &destination) const
{
    for (const auto &[id, p] : m_permits)
        if (p.destination == destination && !p.finished && !p.revoked)
            return true;
    return false;
}

std::expected<PermitId, PermitRefusal> WriteCoordinator::requestPermit(DocumentId document,
                                                                       const DestinationKey &destination,
                                                                       std::uint64_t revision)
{
    if (const auto owner = ownerOf(destination); owner && *owner != document)
        return std::unexpected(PermitRefusal::DestinationOwnedByOtherDocument);
    if (hasActivePermit(destination))
        return std::unexpected(PermitRefusal::WriteInProgress);
    const PermitId id{m_nextPermit++};
    m_permits.emplace(id, Permit{document, destination, revision, association(document)});
    return id;
}

void WriteCoordinator::revoke(PermitId permit)
{
    if (auto it = m_permits.find(permit); it != m_permits.end() && !it->second.started)
        it->second.revoked = true;
}

std::expected<void, WriteRefusal> WriteCoordinator::write(PermitId permit, std::vector<std::byte> snapshot)
{
    const auto it = m_permits.find(permit);
    if (it == m_permits.end())
        return std::unexpected(WriteRefusal::UnknownPermit);
    Permit &p = it->second;
    if (p.revoked)
        return std::unexpected(WriteRefusal::Revoked);
    if (p.started)
        return std::unexpected(WriteRefusal::AlreadyStarted);
    // The destination must still be free of other Documents, and the requester's
    // own association must be what it was when the permit was granted.
    if (const auto owner = ownerOf(p.destination); owner && *owner != p.document)
        return std::unexpected(WriteRefusal::StaleDestination);
    if (association(p.document) != p.associationAtGrant)
        return std::unexpected(WriteRefusal::StaleDestination);
    p.started = true;
    m_port.startWrite(permit, p.destination, std::move(snapshot));
    return {};
}

void WriteCoordinator::cancel(PermitId permit)
{
    const auto it = m_permits.find(permit);
    if (it == m_permits.end() || it->second.finished)
        return;
    if (!it->second.started) {
        it->second.revoked = true;
        return;
    }
    // Only the adapter knows whether the write already published; it reports.
    m_port.requestCancel(permit);
}

void WriteCoordinator::complete(PermitId permit, WriteOutcome outcome)
{
    const auto it = m_permits.find(permit);
    if (it == m_permits.end() || !it->second.started || it->second.finished) {
        ++m_ignoredReports;
        return;
    }
    Permit &p = it->second;
    p.finished = true;
    if (m_onResult)
        m_onResult(WriteResult{permit, p.document, p.destination, p.revision, outcome});
}

} // namespace hikari::application
