#include "hikari/app/api_host.h"

#include "hikari/backends/subtitle_renderer_selection.h"

#include <QCoreApplication>
#include <QFile>
#include <QJSValueIterator>
#include <QSaveFile>

namespace hikari::app {

namespace {

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

}

ApiHost::ApiHost(Wiring wiring, QObject *parent)
    : QObject(parent), m_wiring(std::move(wiring)),
      m_skills(m_wiring.skills.empty() ? nullptr : std::make_unique<api::FolderSkills>(m_wiring.skills)),
      m_api(api::Host{this, this, this, m_wiring.settings, m_wiring.media, this, &m_review, this, m_skills.get(),
                     m_wiring.automation, this, this})
{
}

ApiHost::~ApiHost() = default;

application::visual::perspective::TextMeasure ApiHost::measure() const
{
    return m_wiring.measure ? m_wiring.measure() : application::visual::perspective::TextMeasure{};
}

application::EditSession *ApiHost::session(application::DocumentId document) const
{
    return m_wiring.workspace.title(document) ? m_wiring.files.session(document) : nullptr;
}

std::optional<std::string> ApiHost::path(application::DocumentId document) const
{
    const auto destination = m_wiring.files.destination(document);
    return destination ? std::optional(destination->value) : std::nullopt;
}

bool ApiHost::select(application::DocumentId document, const application::Selection &selection)
{
    if (m_wiring.workspace.editingTarget() == document) {
        if (!m_wiring.selectTarget(selection))
            return false;
    } else if (auto *s = session(document)) {
        s->setSelection(selection);
    } else {
        return false;
    }
    observe();
    return true;
}

void ApiHost::committed(application::DocumentId document)
{
    m_wiring.refresh(document);
    observe();
}

void ApiHost::observe()
{
    if (m_observing)
        return;
    m_observing = true;
    const std::vector<application::DocumentId> documents = m_wiring.workspace.documents();
    const auto target = m_wiring.workspace.editingTarget();
    const bool documentsMoved = documents != m_seenDocuments || target != m_seenTarget;
    m_seenDocuments = documents;
    m_seenTarget = target;
    std::erase_if(m_seen, [&](const auto &entry) {
        return std::find(documents.begin(), documents.end(), application::DocumentId{entry.first}) == documents.end();
    });
    if (documentsMoved)
        m_api.documentsChanged();
    for (const application::DocumentId id : documents) {
        const application::EditSession *s = session(id);
        if (!s)
            continue;
        const auto [it, added] = m_seen.try_emplace(id.value, Seen{s->revision(), s->selection()});
        if (added)
            continue;
        if (it->second.revision != s->revision()) {
            it->second.revision = s->revision();
            m_api.documentChanged(id);
        }
        if (!(it->second.selection == s->selection())) {
            it->second.selection = s->selection();
            m_api.selectionChanged(id);
        }
    }
    m_observing = false;
}

std::optional<api::CommandState> ApiHost::state(std::string_view command) const
{
    if (!m_commandHandler.isCallable())
        return std::nullopt;
    const QJSValue answer = QJSValue(m_commandHandler).call({QJSValue(qs(command)), QJSValue(false)});
    if (!answer.isObject())
        return std::nullopt;
    api::CommandState state;
    state.enabled = answer.property(QStringLiteral("enabled")).toBool();
    state.disabledReason = answer.property(QStringLiteral("reason")).toString().toStdString();
    if (m_wiring.keys)
        state.keys = m_wiring.keys(command);
    return state;
}

bool ApiHost::run(std::string_view command)
{
    if (!m_commandHandler.isCallable())
        return false;
    const bool ran = QJSValue(m_commandHandler).call({QJSValue(qs(command)), QJSValue(true)}).toBool();
    observe();
    return ran;
}

void ApiHost::commandRan(const QString &command, bool ok)
{
    m_api.commandRan(command.toStdString(), ok);
}

void ApiHost::panelChanged(const QString &panel)
{
    m_api.panelChanged(panel.toStdString());
}

QJSValue ApiHost::callPanels(std::string_view panel, const char *action) const
{
    if (!m_panelHandler.isCallable())
        return {};
    return QJSValue(m_panelHandler).call({QJSValue(qs(panel)), QJSValue(QString::fromLatin1(action))});
}

std::vector<api::PanelState> ApiHost::panels() const
{
    std::vector<api::PanelState> out;
    const QJSValue list = callPanels({}, "list");
    const int count = list.property(QStringLiteral("length")).toInt();
    for (int i = 0; i < count; ++i) {
        const QJSValue p = list.property(quint32(i));
        out.push_back(api::PanelState{p.property(QStringLiteral("panel")).toString().toStdString(),
                                      p.property(QStringLiteral("title")).toString().toStdString(),
                                      p.property(QStringLiteral("open")).toBool(),
                                      p.property(QStringLiteral("floating")).toBool(),
                                      p.property(QStringLiteral("focused")).toBool()});
    }
    return out;
}

bool ApiHost::setOpen(std::string_view panel, bool open)
{
    return callPanels(panel, open ? "open" : "close").toBool();
}

bool ApiHost::focus(std::string_view panel)
{
    return callPanels(panel, "focus").toBool();
}

std::optional<std::string> ApiHost::read(const std::string &path, std::size_t atMost) const
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.read(qint64(atMost)).toStdString();
}

bool ApiHost::write(const std::string &path, std::string_view bytes)
{
    QSaveFile file(QString::fromStdString(path));
    return file.open(QIODevice::WriteOnly) && file.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size()) &&
           file.commit();
}

std::optional<std::string> ApiHost::projectFolder(application::DocumentId document) const
{
    const auto folder = m_wiring.translationFolder ? m_wiring.translationFolder(document) : std::nullopt;
    if (!folder)
        return std::nullopt;
    const std::u8string path = folder->u8string();
    return std::string(reinterpret_cast<const char *>(path.data()), path.size());
}

std::optional<application::OverlayFrame> ApiHost::render(application::DocumentId id, const core::Document &document,
                                                         std::int64_t frame, int width, int height)
{
    const application::VideoSession *video = m_wiring.video ? m_wiring.video() : nullptr;
    if (!video || !m_wiring.encode || video->state() != application::VideoSession::State::Ready || frame < 0 ||
        frame >= video->frameCount())
        return std::nullopt;
    const auto start = video->frameStart(int(frame));
    if (!start)
        return std::nullopt;
    if (!m_renderer) {
        m_renderer = std::make_unique<backends::SubtitleRendererSelection>();
        m_renderer->setCsriFolder(std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / u"Csri");
    }
    if (m_renderer->select(m_wiring.renderer ? m_wiring.renderer() : std::string()) || !m_overlay)
        m_overlay = std::make_unique<application::DocumentOverlay>(*m_renderer, m_wiring.encode);
    auto overlay = m_overlay->render(document, m_wiring.fonts ? m_wiring.fonts(id) : std::vector<application::FontLease>{},
                                     *start, width, height);
    return overlay ? std::optional(std::move(*overlay)) : std::nullopt;
}

}
