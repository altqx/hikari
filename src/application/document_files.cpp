#include "hikari/application/document_files.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/srv3.h"
#include "hikari/core/subtitle_load.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace hikari::application {

namespace {

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

}

DocumentFiles::DocumentFiles(FileReadPort &reader, WriteCoordinator &writes) : m_reader(reader), m_writes(writes) {}

DocumentFiles::Entry *DocumentFiles::find(DocumentId document)
{
    const auto it = m_documents.find(document);
    return it == m_documents.end() ? nullptr : &it->second;
}

std::expected<StagedOpen, StageFailure> DocumentFiles::stage(const DestinationKey &destination, const ToUtf8 &toUtf8)
{
    auto bytes = m_reader.read(destination);
    if (!bytes)
        return std::unexpected(StageFailure{openError(bytes.error()), std::nullopt});
    const std::u8string extension = extensionOf(destination.value);
    if (toUtf8) {
        auto text = toUtf8(*bytes);
        if (!text)
            return std::unexpected(StageFailure{OpenError::NotInEncoding, std::nullopt});
        auto load = core::loadSubtitle(*text, extension);
        if (!load)
            return std::unexpected(StageFailure{OpenError::InvalidFormat, std::nullopt});
        StagedOpen staged{destination, std::move(*load), std::nullopt, 0, 0};
        staged.fileBytes = std::move(*bytes);
        staged.converted = true;
        return staged;
    }
    if (const auto format = core::lyricsFormatOf(extension)) {
        if (core::lyricsEncrypted(*bytes, *format))
            return std::unexpected(StageFailure{OpenError::MalformedLyrics, core::LyricsProblem{core::LyricsProblem::Kind::Encrypted}});
        std::optional<std::vector<std::byte>> text = *bytes;
        if (m_lyricsDecoder)
            text = m_lyricsDecoder(*bytes);
        if (!text)
            return std::unexpected(StageFailure{OpenError::MalformedLyrics, core::LyricsProblem{core::LyricsProblem::Kind::NotUtf8}});
        auto imported = core::readLyrics(*text, *format).transform([](core::ImportedLyrics &&l) { return std::move(l.load); });
        if (!imported)
            return std::unexpected(StageFailure{OpenError::MalformedLyrics, imported.error()});
        StagedOpen staged{destination, std::move(*imported), std::nullopt, 0, 0};
        staged.imported = true;
        return staged;
    }
    if (core::isSrv3Extension(extension)) {
        const auto ass = core::assFromSrv3(*bytes);
        if (!ass)
            return std::unexpected(StageFailure{OpenError::MalformedSrv3, std::nullopt, ass.error()});
        StagedOpen staged{destination, core::loadAss(*ass), std::nullopt, 0, 0};
        staged.imported = true;
        return staged;
    }
    auto load = core::loadSubtitle(*bytes, extension);
    if (!load)
        return std::unexpected(StageFailure{OpenError::InvalidFormat, std::nullopt});
    return StagedOpen{destination, std::move(*load), std::nullopt, 0, 0};
}

std::expected<StagedOpen, StageFailure> DocumentFiles::stageOpen(const DestinationKey &destination)
{
    return stage(destination);
}

std::expected<StagedOpen, StageFailure> DocumentFiles::stageReload(DocumentId document, const ToUtf8 &toUtf8)
{
    Entry *entry = find(document);
    if (!entry)
        return std::unexpected(StageFailure{OpenError::UnknownDocument, std::nullopt});
    auto staged = stage(entry->destination, toUtf8);
    if (staged) {
        staged->replaces = document;
        staged->expectedGeneration = entry->generation;
        staged->expectedRevision = entry->session->revision();
    }
    return staged;
}

std::expected<DocumentId, OpenError> DocumentFiles::activate(StagedOpen staged)
{
    std::vector<std::byte> bytes = staged.fileBytes ? std::move(*staged.fileBytes) : staged.load.document.source().bytes;
    if (staged.replaces) {
        Entry *entry = find(*staged.replaces);
        if (!entry)
            return std::unexpected(OpenError::UnknownDocument);
        if (entry->generation != staged.expectedGeneration || entry->session->revision() != staged.expectedRevision)
            return std::unexpected(OpenError::StaleTarget);
        entry->session = std::make_unique<EditSession>(std::move(staged.load.document));
        if (staged.converted)
            entry->session->markUnsaved();
        else
            entry->session->markSaved(entry->session->contentId());
        entry->knownBytes = std::move(bytes);
        ++entry->generation;
        entry->lastSave.reset();
        return *staged.replaces;
    }
    if (staged.imported)
        return createUnsaved(std::move(staged.load.document));
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

std::string_view defaultScript()
{
    static constexpr std::string_view kDefault =
        "[Script Info]\r\n"
        "Title: HikariSub Ass File\r\n"
        "PlayResX: 1280\r\n"
        "PlayResY: 720\r\n"
        "ScaledBorderAndShadow: yes\r\n"
        "WrapStyle: 0\r\n"
        "ScriptType: v4.00+\r\n"
        "Last Style Storage: Default\r\n"
        "YCbCr Matrix: TV.601\r\n"
        "\r\n"
        "[V4+ Styles]\r\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
        "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
        "MarginL, MarginR, MarginV, Encoding\r\n"
        "Style: Default,Garamond,40,&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1\r\n"
        "\r\n"
        "[Events]\r\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
        "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,\r\n";
    return kDefault;
}

DocumentId DocumentFiles::createNew()
{
    const std::string_view script = defaultScript();
    std::vector<std::byte> bytes(script.size());
    std::memcpy(bytes.data(), script.data(), script.size());
    const DocumentId id{m_nextDocument++};
    Entry entry;
    entry.session = std::make_unique<EditSession>(core::loadAss(bytes).document);
    entry.session->markSaved(entry.session->contentId());
    entry.session->markNewDocument();
    if (const auto lines = entry.session->document().lines(); !lines.empty())
        entry.session->setSelection(Selection{lines.front()->id, {lines.front()->id}, lines.front()->id, {}});
    m_documents.emplace(id, std::move(entry));
    return id;
}

DocumentId DocumentFiles::createUnsaved(core::Document document)
{
    const DocumentId id{m_nextDocument++};
    Entry entry;
    entry.session = std::make_unique<EditSession>(std::move(document));
    entry.session->markUnsaved();
    m_documents.emplace(id, std::move(entry));
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
    if (m_beforeSave)
        m_beforeSave(document);
    Entry *entry = find(document);
    if (!entry)
        return std::unexpected(SaveRefusal::UnknownDocument);
    const DestinationKey destination = saveAs ? *saveAs : entry->destination;
    if (destination.value.empty())
        return std::unexpected(SaveRefusal::NoDestination);
    auto snapshot = entry->session->prepareSave();
    if (!snapshot)
        return std::unexpected(SaveRefusal::InvalidDraft);
    auto activeLine = activeLineToSave(*entry->session);
    auto layout = layoutToSave(document, snapshot->document);
    core::Document saved = withActiveLine(std::move(snapshot->document), activeLine);
    if (layout)
        saved.setHikariProperty(kLayoutKey, *layout);
    applyProjectGarbage(document, destination, saved, activeLine);
    auto deferred = m_deferredEncoder ? m_deferredEncoder(document, destination) : std::nullopt;
    std::vector<std::byte> bytes;
    if (!deferred) {
        bytes = core::encodeSubtitle(saved, m_saveOptions);
        if (!m_generatorVersion.empty() && saved.format() == core::SubtitleFormat::Ass)
            bytes = core::withGeneratorComment(std::move(bytes), u8"Script generated by HikariSub v" + m_generatorVersion,
                                               u8"https://hikarisub.org/");
    }
    return SavePlan{document,
                    entry->generation,
                    snapshot->revision,
                    snapshot->content,
                    destination,
                    std::move(bytes),
                    saveAs.has_value() && *saveAs != entry->destination,
                    deferred ? std::nullopt : std::move(activeLine),
                    deferred ? std::nullopt : std::move(layout),
                    std::move(deferred)};
}

std::optional<std::u8string> DocumentFiles::layoutToSave(DocumentId document, const core::Document &saved) const
{
    if (!m_layoutSource || saved.format() != core::SubtitleFormat::Ass)
        return std::nullopt;
    return m_layoutSource(document);
}

void DocumentFiles::applyProjectGarbage(DocumentId document, const DestinationKey &destination, core::Document &saved,
                                        const std::optional<std::u8string> &activeLine) const
{
    if (saved.format() != core::SubtitleFormat::Ass)
        return;
    std::vector<ProjectGarbageEdit> edits;
    if (m_projectGarbageSource)
        edits = m_projectGarbageSource(document, destination);
    if (activeLine)
        edits.push_back({std::u8string(kActiveLineKey), *activeLine});
    static constexpr std::u8string_view kOrder[] = {
        u8"Automation Scripts", u8"Export Filters",   u8"Export Encoding", u8"Last Style Storage", u8"Audio File",
        u8"Video File",         u8"Timecodes File",   u8"Keyframes File",  u8"Video AR Mode",      u8"Video AR Value",
        u8"Video Zoom Percent", u8"Scroll Position",  u8"Active Line",     u8"Video Position"};
    const auto rank = [](const ProjectGarbageEdit &e) {
        return std::ranges::find(kOrder, std::u8string_view(e.key)) - std::begin(kOrder);
    };
    std::ranges::stable_sort(edits, {}, rank);
    for (const auto &edit : edits) {
        if (edit.value)
            saved.setProjectGarbage(edit.key, *edit.value);
        else
            saved.removeProjectGarbage(edit.key);
    }
}

std::expected<PermitId, SaveRefusal> DocumentFiles::startSave(SavePlan plan, bool overwriteExternalChange)
{
    Entry *entry = find(plan.document);
    if (!entry)
        return std::unexpected(SaveRefusal::UnknownDocument);
    if (entry->generation != plan.generation || entry->session->revision() != plan.revision)
        return std::unexpected(SaveRefusal::StalePlan);
    auto current = m_reader.read(plan.destination);
    if (!plan.saveAs && !overwriteExternalChange && (!current || *current != entry->knownBytes))
        return std::unexpected(SaveRefusal::ExternalChange);
    auto permit = m_writes.requestPermit(plan.document, plan.destination, plan.revision);
    if (!permit)
        return std::unexpected(permit.error() == PermitRefusal::DestinationOwnedByOtherDocument
                                   ? SaveRefusal::Collision
                                   : SaveRefusal::WriteInProgress);
    m_pending.emplace(*permit, Pending{plan.document, plan.generation, plan.content, plan.destination, plan.bytes,
                                       plan.saveAs, plan.activeLine, plan.layout,
                                       current ? std::optional(std::move(*current)) : std::nullopt});
    entry->lastSave = SaveStatus{*permit, std::nullopt};
    if (plan.deferred) {
        (*plan.deferred)([this, permit = *permit](std::optional<std::vector<std::byte>> bytes) { writeDeferred(permit, std::move(bytes)); });
        return *permit;
    }
    if (!m_writes.write(*permit, std::move(plan.bytes))) {
        m_pending.erase(*permit);
        entry->lastSave.reset();
        return std::unexpected(SaveRefusal::WriteInProgress);
    }
    return *permit;
}

bool DocumentFiles::acceptExternalChange(DocumentId document)
{
    Entry *entry = find(document);
    if (!entry || entry->destination.value.empty())
        return false;
    auto current = m_reader.read(entry->destination);
    if (!current)
        return false;
    entry->knownBytes = std::move(*current);
    return true;
}

void DocumentFiles::writeDeferred(PermitId permit, std::optional<std::vector<std::byte>> bytes)
{
    const auto it = m_pending.find(permit);
    if (it == m_pending.end())
        return;
    if (bytes) {
        it->second.bytes = *bytes;
        if (m_writes.write(permit, std::move(*bytes)))
            return;
    }
    m_writes.abandon(permit);
}

void DocumentFiles::onWriteResult(const WriteResult &result)
{
    const auto it = m_pending.find(result.permit);
    if (it == m_pending.end())
        return;
    Pending pending = std::move(it->second);
    m_pending.erase(it);
    const bool published =
        result.outcome == WriteOutcome::Written || result.outcome == WriteOutcome::DurabilityUncertain;
    if (published && m_savedOver && pending.previous && *pending.previous != pending.bytes)
        m_savedOver(pending.destination, std::move(*pending.previous));
    Entry *entry = find(pending.document);
    if (!entry || entry->generation != pending.generation)
        return;
    if (entry->lastSave && entry->lastSave->permit == result.permit)
        entry->lastSave->outcome = result.outcome;
    if (!published)
        return;
    entry->session->markSaved(pending.content);
    if (pending.activeLine)
        amendActiveLine(*entry->session, *pending.activeLine);
    if (pending.layout)
        entry->session->amendHikariProperty(kLayoutKey, *pending.layout);
    entry->knownBytes = std::move(pending.bytes);
    if (pending.saveAs) {
        entry->destination = pending.destination;
        m_writes.associate(pending.document, pending.destination);
    }
}

std::optional<std::u8string> activeLineToSave(const EditSession &session)
{
    const core::Document &document = session.document();
    if (document.format() != core::SubtitleFormat::Ass)
        return std::nullopt;
    std::size_t row = 0;
    if (const auto active = session.selection().active) {
        const auto lines = document.lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id == *active) {
                row = i;
                break;
            }
    }
    const std::string text = std::to_string(row);
    return std::u8string(text.begin(), text.end());
}

core::Document withActiveLine(core::Document document, const std::optional<std::u8string> &row)
{
    if (row)
        document.setScriptInfo(kActiveLineKey, *row);
    return document;
}

void amendActiveLine(EditSession &session, const std::u8string &row)
{
    session.amendScriptInfo(kActiveLineKey, row);
    session.amendProjectGarbage(kActiveLineKey, row);
}

std::u8string aegisubDecimal(double value)
{
    const std::string text = std::to_string(value);
    return std::u8string(text.begin(), text.end());
}

std::vector<ProjectGarbageEdit> aspectGarbage(double shown, double own)
{
    if (!(shown > 0) || std::abs(shown - own) < 1e-4)
        return {{u8"Video AR Mode", std::nullopt}, {u8"Video AR Value", std::nullopt}};
    return {{u8"Video AR Mode", u8"4"}, {u8"Video AR Value", aegisubDecimal(shown)}};
}

std::optional<SaveStatus> DocumentFiles::lastSave(DocumentId document) const
{
    const auto it = m_documents.find(document);
    return it == m_documents.end() ? std::nullopt : it->second.lastSave;
}

}
