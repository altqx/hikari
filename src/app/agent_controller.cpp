#include "hikari/app/agent_controller.h"

#include "hikari/app/ai_features.h"
#include "hikari/application/ai_review.h"
#include "hikari/application/document_files.h"
#include "hikari/backends/ai_agent.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/desktop_handoff.h"
#include "cited_answer.h"
#include "search_suggestions_view.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <array>

namespace hikari::app {

namespace ai = hikari::application::ai;
using backends::ai::AgentMode;
using backends::ai::AgentThread;
using backends::ai::AgentThreads;
using api::Json;

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

struct AgentModel {
    const char *program;
    const char *name;
    ai::ModelRef ref() const { return {std::string(backends::ai::kAgentProviderPrefix) + program, ""}; }
    QString text() const { return QCoreApplication::translate("HikariSub.Agent", name); }
};

const std::array kAgentModels{AgentModel{"claude", QT_TRANSLATE_NOOP("HikariSub.Agent", "Claude Code")},
                              AgentModel{"codex", QT_TRANSLATE_NOOP("HikariSub.Agent", "Codex")}};

QString modelValue(const ai::ModelRef &ref)
{
    return qs(ref.providerId) + QLatin1Char('\t') + qs(ref.modelId);
}

const ai::Model *modelIn(const ai::Config &config, const ai::ModelRef &ref)
{
    const ai::Provider *provider = config.provider(ref.providerId);
    if (!provider)
        return nullptr;
    const auto it = std::ranges::find(provider->models, ref.modelId, &ai::Model::id);
    return it == provider->models.end() ? nullptr : &*it;
}

QString dayOf(std::int64_t ms, const QDate &today)
{
    const QDate day = QDateTime::fromMSecsSinceEpoch(ms).date();
    if (day == today)
        return QStringLiteral("today");
    if (day == today.addDays(-1))
        return QStringLiteral("yesterday");
    return day.toString(Qt::ISODate);
}

QVariantMap variantOf(const std::string &dump)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(dump)).object().toVariantMap();
}

std::string at(const Json &item, std::string_view key)
{
    const Json *value = item.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

}

int AgentItemsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant AgentItemsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const QVariantMap &item = m_items[static_cast<std::size_t>(index.row())];
    if (role == KindRole)
        return item.value(QStringLiteral("kind"));
    if (role == ItemRole)
        return item;
    return {};
}

QHash<int, QByteArray> AgentItemsModel::roleNames() const
{
    return {{KindRole, "kind"}, {ItemRole, "item"}};
}

void AgentItemsModel::update(std::vector<std::string> items)
{
    const std::size_t kept = std::min(items.size(), m_dumps.size());
    std::size_t same = 0;
    while (same < kept && items[same] == m_dumps[same])
        ++same;
    if (items.size() < m_dumps.size() && same < items.size()) {
        beginResetModel();
        m_dumps = std::move(items);
        m_items.clear();
        for (const std::string &d : m_dumps)
            m_items.push_back(variantOf(d));
        endResetModel();
        return;
    }
    if (items.size() < m_dumps.size()) {
        beginRemoveRows({}, static_cast<int>(items.size()), static_cast<int>(m_dumps.size()) - 1);
        m_dumps.resize(items.size());
        m_items.resize(items.size());
        endRemoveRows();
    }
    for (std::size_t i = same; i < kept && i < m_dumps.size(); ++i) {
        if (items[i] == m_dumps[i])
            continue;
        m_dumps[i] = items[i];
        m_items[i] = variantOf(items[i]);
        emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)));
    }
    if (items.size() > m_dumps.size()) {
        beginInsertRows({}, static_cast<int>(m_dumps.size()), static_cast<int>(items.size()) - 1);
        for (std::size_t i = m_dumps.size(); i < items.size(); ++i) {
            m_items.push_back(variantOf(items[i]));
            m_dumps.push_back(std::move(items[i]));
        }
        endInsertRows();
    }
}

AgentController::AgentController(Wiring wiring, QObject *parent) : QObject(parent), m_wiring(std::move(wiring))
{
    connect(&m_wiring.ai, &AiFeatures::switched, this, &AgentController::apply);
    connect(&m_wiring.settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
        if (id.startsWith(QLatin1String("ai.agent.")))
            emit settingsChanged();
        if (id == QLatin1String(AiFeatures::kWebSearchSetting) || id == QLatin1String(AiFeatures::kSearchServiceSetting))
            emit threadChanged();
        if (id.startsWith(QLatin1String("ai.")))
            emit modelsChanged();
    });
    m_busySource = m_wiring.ai.addBusySource([this] { return running(); });
    apply();
}

AgentController::~AgentController()
{
    m_wiring.ai.removeBusySource(m_busySource);
    m_threads.reset();
}

bool AgentController::on() const
{
    return m_threads != nullptr;
}

void AgentController::apply()
{
    if (m_wiring.ai.on() == on())
        return;
    if (!m_wiring.ai.on()) {
        m_threads.reset();
        m_store.reset();
        m_attached.clear();
        m_current.clear();
        m_items.update({});
    } else {
        const QString folder = m_wiring.userFolder.isEmpty() ? QString() : m_wiring.userFolder + QStringLiteral("/ai/threads");
        m_store = std::make_unique<backends::ai::ThreadStore>(folder);
        AgentThreads::Wiring w;
        w.requests = [this] { return m_wiring.ai.requests(); };
        w.agentHost = [this] { return m_wiring.ai.sidecarForMcp(); };
        w.config = [this]() -> const ai::Config * {
            const ai::Providers *providers = m_wiring.ai.providers();
            return providers ? &providers->config() : nullptr;
        };
        w.api = &m_wiring.api;
        w.defaults = [this] {
            return AgentThreads::Defaults{
                backends::ai::agentModeFromId(m_wiring.settings.text(kModeSetting).toStdString()).value_or(AgentMode::Auto),
                threadCap(), dailyCap()};
        };
        w.beginTurn = [this](const std::string &caller, const std::string &label) {
            return m_wiring.review.beginTurn(caller, label);
        };
        w.editingTarget = [this] { return m_wiring.workspace.editingTarget(); };
        w.documentTitle = [this](application::DocumentId document) {
            const std::string *title = m_wiring.workspace.title(document);
            return title ? *title : std::string();
        };
        w.safeFolders = [this] { return safeFolders(); };
        w.now = m_wiring.now;
        m_threads = std::make_unique<AgentThreads>(std::move(w), m_store.get());
        connect(m_threads.get(), &AgentThreads::threadsChanged, this, [this] {
            if (!m_current.empty() && !m_threads->thread(m_current))
                select({});
            emit threadsChanged();
        });
        connect(m_threads.get(), &AgentThreads::threadChanged, this, [this](const QString &id) {
            emit threadsChanged();
            if (id.toStdString() == m_current)
                refreshThread();
        });
        connect(m_threads.get(), &AgentThreads::asked, this, [this](quint64 ask) {
            if (const auto it = m_threads->asks().find(ask); it != m_threads->asks().end())
                emit asked(qs(it->second.thread));
        });
        const auto all = m_threads->threads();
        select(all.empty() ? std::string() : all.front()->id);
    }
    emit switched();
    emit threadsChanged();
    emit modelsChanged();
}

void AgentController::select(std::string id)
{
    m_current = std::move(id);
    m_items.update({});
    refreshThread();
    emit currentChanged();
}

void AgentController::refreshThread()
{
    std::vector<std::string> dumps;
    if (const AgentThread *t = m_threads && !m_current.empty() ? m_threads->thread(m_current) : nullptr)
        for (const Json &item : t->items.asArray())
            dumps.push_back(item.dump());
    m_items.update(std::move(dumps));
    emit threadChanged();
}

bool AgentController::ready() const
{
    const ai::Providers *providers = m_wiring.ai.providers();
    return (providers && providers->config().modelFor(ai::Role::Chat).has_value()) || foundAgent().has_value();
}

std::optional<ai::ModelRef> AgentController::foundAgent() const
{
    for (const AgentModel &agent : kAgentModels)
        if (m_wiring.ai.agentFound(QString::fromLatin1(agent.program)))
            return agent.ref();
    return std::nullopt;
}

void AgentController::fallBackToAnAgent(const std::string &thread)
{
    const AgentThread *t = m_threads->thread(thread);
    if (!t || m_threads->modelOf(*t))
        return;
    if (const auto agent = foundAgent())
        m_threads->setModel(thread, *agent);
}

bool AgentController::running() const
{
    if (!m_threads)
        return false;
    const auto all = m_threads->threads();
    return std::ranges::any_of(all, [&](const AgentThread *t) { return m_threads->running(t->id); });
}

QVariantList AgentController::threads() const
{
    QVariantList out;
    if (!m_threads)
        return out;
    const ai::Providers *providers = m_wiring.ai.providers();
    const QDate today = QDate::currentDate();
    for (const AgentThread *t : m_threads->threads()) {
        QString model;
        if (const auto ref = m_threads->modelOf(*t); ref && providers)
            if (const ai::Model *m = modelIn(providers->config(), *ref))
                model = qs(m->name.empty() ? m->id : m->name);
        QStringList files;
        for (const std::string &f : t->files)
            files << qs(f);
        QString state;
        if (m_threads->running(t->id))
            state = QStringLiteral("running");
        else if (t->cap > 0 && t->cost >= t->cap)
            state = QStringLiteral("cap");
        else if (t->mode == AgentMode::FullAccess)
            state = QStringLiteral("full");
        out.push_back(QVariantMap{{QStringLiteral("id"), qs(t->id)},
                                  {QStringLiteral("title"), qs(t->title)},
                                  {QStringLiteral("files"), files.join(QStringLiteral(", "))},
                                  {QStringLiteral("model"), model},
                                  {QStringLiteral("cost"), t->cost},
                                  {QStringLiteral("costUnknown"), t->costUnknown},
                                  {QStringLiteral("state"), state},
                                  {QStringLiteral("day"), dayOf(t->updated, today)}});
    }
    return out;
}

QVariantMap AgentController::thread() const
{
    const AgentThread *t = m_threads && !m_current.empty() ? m_threads->thread(m_current) : nullptr;
    if (!t)
        return {};
    const ai::Providers *providers = m_wiring.ai.providers();
    QString model;
    QString modelText;
    qint64 window = 0;
    if (const auto ref = m_threads->modelOf(*t)) {
        model = modelValue(*ref);
        const auto agent = std::ranges::find_if(kAgentModels, [&](const AgentModel &a) { return a.ref() == *ref; });
        modelText = agent != kAgentModels.end() ? agent->text() : qs(ref->modelId);
        if (const ai::Model *m = providers ? modelIn(providers->config(), *ref) : nullptr) {
            modelText = qs(m->name.empty() ? m->id : m->name);
            window = m->contextTokens.value_or(0);
        }
    }
    const ai::Provider *provider = nullptr;
    if (const auto ref = m_threads->modelOf(*t); ref && providers)
        provider = providers->config().provider(ref->providerId);
    const backends::ai::AiRequests *requests = m_wiring.ai.requests();
    const bool searchReady = (provider && ai::searchesNatively(*provider)) || (requests && requests->searchService());
    QVariantList attached;
    if (const auto it = m_attached.find(m_current); it != m_attached.end())
        for (const application::DocumentId d : it->second)
            if (const std::string *title = m_wiring.workspace.title(d))
                attached.push_back(QVariantMap{{QStringLiteral("id"), qulonglong(d.value)}, {QStringLiteral("title"), qs(*title)}});
    return {{QStringLiteral("id"), qs(t->id)},
            {QStringLiteral("title"), qs(t->title)},
            {QStringLiteral("mode"), qs(std::string(backends::ai::agentModeId(t->mode)))},
            {QStringLiteral("running"), m_threads->running(t->id)},
            {QStringLiteral("queued"), qs(m_threads->queued(t->id))},
            {QStringLiteral("cost"), t->cost},
            {QStringLiteral("cap"), t->cap},
            {QStringLiteral("costUnknown"), t->costUnknown},
            {QStringLiteral("contextTokens"), qint64(t->contextTokens)},
            {QStringLiteral("contextWindow"), window},
            {QStringLiteral("model"), model},
            {QStringLiteral("modelText"), modelText},
            {QStringLiteral("attached"), attached},
            {QStringLiteral("webSearch"), t->webSearch},
            {QStringLiteral("webSearchReady"), searchReady},
            {QStringLiteral("webSearchCacheMiss"), provider && provider->preset == ai::Preset::Anthropic}};
}

QVariantList AgentController::models() const
{
    QVariantList out;
    for (const AgentModel &agent : kAgentModels)
        if (m_wiring.ai.agentFound(QString::fromLatin1(agent.program)))
            out.push_back(QVariantMap{{QStringLiteral("value"), modelValue(agent.ref())},
                                      {QStringLiteral("text"), agent.text()},
                                      {QStringLiteral("provider"), QString()}});
    const ai::Providers *providers = m_wiring.ai.providers();
    if (!providers)
        return out;
    for (const ai::Provider &p : providers->config().providers)
        for (const ai::Model &m : p.models)
            out.push_back(QVariantMap{{QStringLiteral("value"), modelValue({p.id, m.id})},
                                      {QStringLiteral("text"), qs(m.name.empty() ? m.id : m.name)},
                                      {QStringLiteral("provider"), qs(p.name)}});
    return out;
}

QVariantList AgentController::attachable() const
{
    QVariantList out;
    const auto target = m_wiring.workspace.editingTarget();
    for (const application::DocumentId d : m_wiring.workspace.allTabs())
        if (d != target)
            if (const std::string *title = m_wiring.workspace.title(d))
                out.push_back(QVariantMap{{QStringLiteral("id"), qulonglong(d.value)}, {QStringLiteral("title"), qs(*title)}});
    return out;
}

QString AgentController::activeTitle() const
{
    const auto target = m_wiring.workspace.editingTarget();
    const std::string *title = target ? m_wiring.workspace.title(*target) : nullptr;
    return title ? qs(*title) : QString();
}

bool AgentController::costMeter() const
{
    return m_wiring.settings.boolean(kCostMeterSetting);
}

bool AgentController::fullAccessWarned() const
{
    return m_wiring.settings.boolean(kFullAccessWarnedSetting);
}

double AgentController::spentToday() const
{
    return m_threads ? m_threads->spentToday() : 0.0;
}

double AgentController::dailyCap() const
{
    return static_cast<double>(m_wiring.settings.integer64(kDailyCapSetting)) / 100.0;
}

double AgentController::threadCap() const
{
    return static_cast<double>(m_wiring.settings.integer64(kThreadCapSetting)) / 100.0;
}

void AgentController::newThread()
{
    if (!m_threads)
        return;
    const std::string id = m_threads->create();
    fallBackToAnAgent(id);
    select(id);
}

void AgentController::open(const QString &id)
{
    if (m_threads && m_threads->thread(id.toStdString()))
        select(id.toStdString());
}

void AgentController::rename(const QString &id, const QString &title)
{
    if (m_threads && !title.trimmed().isEmpty())
        m_threads->rename(id.toStdString(), title.trimmed().toStdString());
}

void AgentController::remove(const QString &id)
{
    if (!m_threads)
        return;
    m_attached.erase(id.toStdString());
    m_threads->remove(id.toStdString());
}

bool AgentController::webSearchShown() const
{
    return m_wiring.settings.boolean(AiFeatures::kWebSearchSetting);
}

void AgentController::setWebSearch(bool on)
{
    if (m_threads && !m_current.empty())
        m_threads->setWebSearch(m_current, on);
}

QString AgentController::citedText(const QVariantMap &item, const QColor &chip, const QColor &ink) const
{
    return ui::citedAnswerHtml(item.value(QStringLiteral("text")).toString(),
                               item.value(QStringLiteral("citations")).toList(), chip, ink);
}

QString AgentController::siteOf(const QString &url) const
{
    return ui::siteOf(url);
}

bool AgentController::openLink(const QString &url) const
{
    const QUrl target(url, QUrl::StrictMode);
    if (!target.isValid() || (target.scheme() != QLatin1String("https") && target.scheme() != QLatin1String("http")) ||
        target.host().isEmpty() || !target.userInfo().isEmpty())
        return false;
    return backends::desktop::openUrl(target);
}

QVariantMap AgentController::suggestions(const QString &html) const
{
    return ui::searchSuggestionsView(html.toStdString());
}

QString AgentController::markdown(const QString &id) const
{
    const AgentThread *t = m_threads ? m_threads->thread(id.toStdString()) : nullptr;
    if (!t)
        return {};
    QString out = QStringLiteral("# ") + qs(t->title) + QStringLiteral("\n\n");
    for (const Json &item : t->items.asArray()) {
        const std::string kind = at(item, "kind");
        if (kind == "user")
            out += QStringLiteral("**You:** ") + qs(at(item, "text")) + QStringLiteral("\n\n");
        else if (kind == "text") {
            out += qs(at(item, "text")) + QStringLiteral("\n\n");
            if (const Json *citations = item.find("citations"); citations && citations->isArray())
                for (const Json &c : citations->asArray())
                    out += QStringLiteral("[%1] [%2](%3)\n").arg(QString::number(c.find("index") ? c.find("index")->asNumber() : 0),
                                                              qs(at(c, "title")), qs(at(c, "url")));
            if (item.find("citations"))
                out += QLatin1Char('\n');
        }
        else if (kind == "change")
            out += QStringLiteral("- ") + qs(at(item, "name")) + QStringLiteral(" (") + qs(at(item, "title")) +
                   QStringLiteral(")\n\n");
    }
    return out;
}

bool AgentController::exportMarkdown(const QString &id, const QUrl &file) const
{
    const QString text = markdown(id);
    if (text.isEmpty() || !file.isLocalFile())
        return false;
    QSaveFile out(file.toLocalFile());
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(text.toUtf8());
    return out.commit();
}

void AgentController::send(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (!m_threads || trimmed.isEmpty())
        return;
    if (m_current.empty() || !m_threads->thread(m_current))
        newThread();
    fallBackToAnAgent(m_current);
    m_threads->setAttached(m_current, m_attached[m_current]);
    m_threads->send(m_current, trimmed.toStdString());
}

void AgentController::stop()
{
    if (m_threads)
        m_threads->stop(m_current);
}

void AgentController::steer()
{
    if (m_threads)
        m_threads->steer(m_current);
}

void AgentController::setQueued(const QString &text)
{
    if (m_threads)
        m_threads->setQueued(m_current, text.toStdString());
}

void AgentController::retry()
{
    if (m_threads)
        m_threads->retry(m_current);
}

void AgentController::raiseCap()
{
    if (m_threads)
        m_threads->raiseCap(m_current);
}

void AgentController::setMode(const QString &mode)
{
    if (const auto m = backends::ai::agentModeFromId(mode.toStdString()); m && m_threads)
        m_threads->setMode(m_current, *m);
}

void AgentController::setModel(const QString &value)
{
    const auto parts = value.split(QLatin1Char('\t'));
    if (m_threads && parts.size() == 2)
        m_threads->setModel(m_current, {parts[0].toStdString(), parts[1].toStdString()});
}

void AgentController::answer(qulonglong ask, const QString &answer, const QString &message)
{
    if (!m_threads)
        return;
    const AgentThreads::Answer a = answer == QLatin1String("allowInThread") ? AgentThreads::Answer::AllowInThread
                                   : answer == QLatin1String("deny")        ? AgentThreads::Answer::Deny
                                                                            : AgentThreads::Answer::AllowOnce;
    m_threads->answer(ask, a, message.trimmed().toStdString());
}

void AgentController::attach(qulonglong document)
{
    auto &attached = m_attached[m_current];
    const application::DocumentId d{document};
    if (std::ranges::find(attached, d) == attached.end() && m_wiring.workspace.title(d))
        attached.push_back(d);
    emit threadChanged();
}

void AgentController::detach(qulonglong document)
{
    std::erase(m_attached[m_current], application::DocumentId{document});
    emit threadChanged();
}

void AgentController::setFullAccessWarned()
{
    m_wiring.settings.set(kFullAccessWarnedSetting, true);
}

std::optional<std::string> AgentController::pathOf(application::DocumentId document) const
{
    const auto destination = m_wiring.files.destination(document);
    if (!destination || destination->value.empty())
        return std::nullopt;
    return destination->value;
}

std::vector<std::string> AgentController::safeFolders() const
{
    std::vector<std::string> folders;
    if (!m_wiring.userFolder.isEmpty())
        folders.push_back(m_wiring.userFolder.toStdString());
    for (const application::DocumentId d : m_wiring.workspace.allTabs())
        if (const auto path = pathOf(d))
            folders.push_back(QFileInfo(qs(*path)).absolutePath().toStdString());
    return folders;
}

}
