#include "hikari/app/ai_settings.h"

#include "hikari/app/ai_acp_settings.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/ai_mcp.h"
#include "hikari/app/ai_mcp_servers.h"
#include "hikari/backends/ai_agent_cli.h"
#include "hikari/backends/ai_antigravity.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_updater.h"
#include "hikari/backends/ai_signin.h"
#include "hikari/backends/desktop_handoff.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>

namespace hikari::app {

namespace ai = application::ai;

namespace {

QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

std::string str(const QVariant &value)
{
    return value.toString().toStdString();
}

std::string freshId(const ai::Config &config, ai::Preset preset)
{
    std::string id(ai::presetId(preset));
    for (int n = 2; config.provider(id); ++n)
        id = std::string(ai::presetId(preset)) + '-' + std::to_string(n);
    return id;
}

QString modelValue(const std::string &providerId, const std::string &modelId)
{
    return qs(providerId) + QLatin1Char('\t') + qs(modelId);
}

QString saveErrorId(ai::SaveError error)
{
    switch (error) {
    case ai::SaveError::Key:
        return QStringLiteral("key");
    case ai::SaveError::KeychainUnavailable:
        return QStringLiteral("keychainUnavailable");
    case ai::SaveError::KeychainDenied:
        return QStringLiteral("keychainDenied");
    case ai::SaveError::KeychainNotPrivate:
        return QStringLiteral("keychainNotPrivate");
    case ai::SaveError::Config:
        return QStringLiteral("config");
    }
    return QStringLiteral("config");
}

QString downloadErrorId(backends::ai::SidecarDownloadError error)
{
    using E = backends::ai::SidecarDownloadError;
    switch (error) {
    case E::Manifest:
    case E::NoAsset:
        return QStringLiteral("downloadManifest");
    case E::Signature:
        return QStringLiteral("downloadSignature");
    case E::Network:
        return QStringLiteral("downloadNetwork");
    case E::Mismatch:
    case E::Unpack:
        return QStringLiteral("downloadDamaged");
    case E::Disk:
        return QStringLiteral("downloadDisk");
    }
    return QStringLiteral("downloadNetwork");
}

QString methodId(backends::ai::AgentInstallMethod method)
{
    using M = backends::ai::AgentInstallMethod;
    switch (method) {
    case M::Native:
        return QStringLiteral("native");
    case M::Npm:
        return QStringLiteral("npm");
    case M::Pnpm:
        return QStringLiteral("pnpm");
    case M::Bun:
        return QStringLiteral("bun");
    case M::Yarn:
        return QStringLiteral("yarn");
    case M::Volta:
        return QStringLiteral("volta");
    case M::Homebrew:
        return QStringLiteral("homebrew");
    case M::WinGet:
        return QStringLiteral("winget");
    case M::Chocolatey:
        return QStringLiteral("chocolatey");
    case M::Mise:
        return QStringLiteral("mise");
    case M::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString badgeId(backends::ai::AgentBadge badge)
{
    using B = backends::ai::AgentBadge;
    switch (badge) {
    case B::Current:
        return QStringLiteral("current");
    case B::UpdateAvailable:
        return QStringLiteral("update");
    case B::Unsupported:
        return QStringLiteral("unsupported");
    case B::KnownBroken:
        return QStringLiteral("broken");
    case B::Unknown:
        break;
    }
    return {};
}

QString outcomeId(backends::ai::AgentUpdate::Outcome outcome)
{
    using O = backends::ai::AgentUpdate::Outcome;
    switch (outcome) {
    case O::Updated:
        return QStringLiteral("updated");
    case O::Unchanged:
        return QStringLiteral("unchanged");
    case O::TimedOut:
        return QStringLiteral("timedOut");
    case O::Cancelled:
        return QStringLiteral("cancelled");
    case O::Failed:
        break;
    }
    return QStringLiteral("failed");
}

QString shownPath(const QString &path)
{
#ifndef Q_OS_WIN
    const QString home = QDir::homePath() + QLatin1Char('/');
    if (path.startsWith(home))
        return QStringLiteral("~/") + path.mid(home.size());
#endif
    return QDir::toNativeSeparators(path);
}

bool sameEndpoint(const ai::Provider &a, const ai::Provider &b)
{
    return a.id == b.id && a.preset == b.preset && a.wire == b.wire && a.baseUrl == b.baseUrl;
}

}

namespace {

constexpr std::array kAcpClis{"opencode", "agy"};

bool isAcpCli(const QString &name)
{
    return std::ranges::any_of(kAcpClis, [&](const char *cli) { return name == QLatin1String(cli); });
}

QString unresolvedShimText(const QString &shim)
{
    if (shim.isEmpty())
        return {};
    const QString shown = QDir::toNativeSeparators(shim);
    return backends::ai::agentShimManager(shim) == QLatin1String("asdf")
               ? QCoreApplication::translate("HikariSub.Options", "Found an asdf shim that asdf couldn’t resolve: %1").arg(shown)
               : QCoreApplication::translate("HikariSub.Options", "Found a mise shim that mise couldn’t resolve: %1").arg(shown);
}

backends::ai::AgentInstall acpInstall(const QString &name)
{
    return {.manualCommand = name == QLatin1String("agy") ? QStringLiteral("agy update") : QString()};
}

}

struct AiSettings::Agent {
    std::optional<backends::ai::AgentCli> cli;
    QString missing;
    backends::ai::AgentInstall install;
    QString latest;
    bool session = false;
    std::unique_ptr<backends::ai::AgentUpdate> update;
    QString outcome;
    QString lastLine;
};

AiSettings::AiSettings(AiFeatures &features, AiMcp *mcp, AiMcpServers *mcpServers, QObject *parent)
    : QObject(parent), m_features(features), m_mcp(mcp), m_mcpServers(mcpServers)
{
    for (const char *name : backends::ai::kAgentClis)
        m_agents.emplace(QLatin1String(name), std::make_unique<Agent>());
    for (const char *name : kAcpClis)
        m_agents.emplace(QLatin1String(name), std::make_unique<Agent>());
    m_acp = new AiAcpSettings(m_features, this);
    connect(&m_features, &AiFeatures::switched, this, [this] {
        reset();
        watchUpdater();
        emit switched();
        emit providersChanged();
        emit componentsChanged();
    });
    connect(&m_features, &AiFeatures::componentsChanged, this, &AiSettings::componentsChanged);
    watchUpdater();
}

AiSettings::~AiSettings()
{
    *m_alive = false;
}

bool AiSettings::sandboxed() const
{
    return m_features.sandboxed();
}

QObject *AiSettings::mcp() const
{
    return m_mcp;
}

QObject *AiSettings::acp() const
{
    return m_acp;
}

QObject *AiSettings::mcpServers() const
{
    return m_mcpServers;
}

bool AiSettings::on() const
{
    return m_features.on();
}

QVariantList AiSettings::providers() const
{
    QVariantList out;
    const ai::Providers *providers = m_features.providers();
    if (!providers)
        return out;
    for (const ai::Provider &p : providers->config().providers) {
        const auto it = m_status.find(p.id);
        const Status status = it == m_status.end() ? Status{} : it->second;
        out.push_back(QVariantMap{{QStringLiteral("id"), qs(p.id)},
                                  {QStringLiteral("name"), qs(p.name)},
                                  {QStringLiteral("preset"), qs(ai::presetId(p.preset))},
                                  {QStringLiteral("wire"), qs(ai::wireId(p.wire))},
                                  {QStringLiteral("baseUrl"), qs(p.baseUrl)},
                                  {QStringLiteral("needsKey"), p.needsKey},
                                  {QStringLiteral("signedIn"), p.signedIn},
                                  {QStringLiteral("searchesNatively"), ai::searchesNatively(p)},
                                  {QStringLiteral("models"), int(p.models.size())},
                                  {QStringLiteral("state"), status.state},
                                  {QStringLiteral("error"), status.error},
                                  {QStringLiteral("httpStatus"), status.httpStatus},
                                  {QStringLiteral("detail"), status.detail}});
    }
    return out;
}

QVariantList AiSettings::modelChoices() const
{
    QVariantList out;
    const ai::Providers *providers = m_features.providers();
    if (!providers)
        return out;
    for (const ai::Provider &p : providers->config().providers)
        for (const ai::Model &m : p.models)
            out.push_back(QVariantMap{{QStringLiteral("value"), modelValue(p.id, m.id)},
                                      {QStringLiteral("text"), qs(m.name.empty() ? m.id : m.name)},
                                      {QStringLiteral("provider"), qs(p.name)},
                                      {QStringLiteral("vision"), ai::capabilitiesOf(p, m.id).vision}});
    return out;
}

QVariantMap AiSettings::roles() const
{
    QVariantMap out;
    const ai::Providers *providers = m_features.providers();
    for (const ai::Role role : ai::kRoles) {
        QString value;
        if (providers)
            if (const auto it = providers->config().roles.find(role); it != providers->config().roles.end())
                value = modelValue(it->second.providerId, it->second.modelId);
        out.insert(qs(ai::roleId(role)), value);
    }
    return out;
}

QVariantMap AiSettings::download() const
{
    return {{QStringLiteral("needed"), m_download != nullptr},
            {QStringLiteral("size"), m_downloadSize},
            {QStringLiteral("received"), m_downloadReceived},
            {QStringLiteral("active"), m_installing},
            {QStringLiteral("error"), m_downloadError}};
}

QVariantMap AiSettings::form(const QString &providerId) const
{
    const ai::Providers *providers = m_features.providers();
    const ai::Provider *p = providers ? providers->config().provider(providerId.toStdString()) : nullptr;
    if (!p)
        return {};
    return {{QStringLiteral("id"), qs(p->id)},
            {QStringLiteral("preset"), qs(ai::presetId(p->preset))},
            {QStringLiteral("name"), qs(p->name)},
            {QStringLiteral("baseUrl"), qs(p->baseUrl)},
            {QStringLiteral("wire"), qs(ai::wireId(p->wire))},
            {QStringLiteral("needsKey"), p->needsKey},
            {QStringLiteral("signedIn"), p->signedIn}};
}

QString AiSettings::newProviderId(const QString &preset) const
{
    const ai::Providers *providers = m_features.providers();
    const auto kind = ai::presetFromId(preset.toStdString());
    return providers && kind ? qs(freshId(providers->config(), *kind)) : QString();
}

std::optional<ai::Provider> AiSettings::draftProvider(const QVariantMap &form) const
{
    const ai::Providers *providers = m_features.providers();
    if (!providers)
        return std::nullopt;
    const std::string id = str(form.value(QStringLiteral("id")));
    const std::string key = str(form.value(QStringLiteral("key")));
    ai::Provider p;
    if (const ai::Provider *existing = id.empty() ? nullptr : providers->config().provider(id)) {
        p = *existing;
        if (!key.empty())
            p.signedIn = false;
    } else {
        const auto preset = ai::presetFromId(str(form.value(QStringLiteral("preset"))));
        if (!preset)
            return std::nullopt;
        p = ai::presetProvider(*preset, freshId(providers->config(), *preset), {});
        if (*preset == ai::Preset::Custom)
            p.needsKey = !key.empty();
    }
    p.name = str(form.value(QStringLiteral("name")).toString().trimmed());
    if (p.name.empty())
        return std::nullopt;
    if (p.preset == ai::Preset::Custom) {
        const QString base = form.value(QStringLiteral("baseUrl")).toString().trimmed();
        const QUrl url(base);
        if (!url.isValid() || url.host().isEmpty() ||
            (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
            return std::nullopt;
        p.baseUrl = base.toStdString();
        const auto wire = ai::wireFromId(str(form.value(QStringLiteral("wire"))));
        if (wire == ai::Wire::Responses || wire == ai::Wire::ChatCompletions)
            p.wire = *wire;
        if (!key.empty())
            p.needsKey = true;
    }
    return p;
}

void AiSettings::setDraft(const QString &state, const QString &error, int httpStatus, const QString &detail,
                          int models)
{
    m_draft = {{QStringLiteral("state"), state},
               {QStringLiteral("error"), error},
               {QStringLiteral("httpStatus"), httpStatus},
               {QStringLiteral("detail"), detail},
               {QStringLiteral("models"), models}};
    emit draftChanged();
}

void AiSettings::prepareDownload()
{
    if (m_download || !m_features.on())
        return;
    backends::ai::AiSidecarUpdater *updater = m_features.updater();
    if (!updater || updater->current())
        return;
    m_download = updater;
    using backends::ai::AiSidecarUpdater;
    connect(updater, &AiSidecarUpdater::described, this, [this](qint64 bytes) {
        m_describing = false;
        m_downloadSize = bytes;
        if (m_installWanted) {
            m_installWanted = false;
            m_installing = true;
            m_download->install();
        }
        emit downloadChanged();
    });
    connect(updater, &AiSidecarUpdater::progress, this, [this](qint64 received, qint64 total) {
        m_downloadReceived = received;
        if (total > 0)
            m_downloadSize = total;
        emit downloadChanged();
    });
    connect(updater, &AiSidecarUpdater::installed, this, [this] {
        auto waiting = std::exchange(m_afterDownload, {});
        m_download->disconnect(this);
        m_download = nullptr;
        m_installing = false;
        m_downloadError.clear();
        emit downloadChanged();
        for (auto &then : waiting)
            then(true);
    });
    connect(updater, &AiSidecarUpdater::failed, this,
            [this](backends::ai::SidecarDownloadError error, const QString &) {
                const bool wasInstalling = m_installing || m_installWanted;
                m_describing = m_installing = m_installWanted = false;
                if (!wasInstalling)
                    return;
                m_downloadError = downloadErrorId(error);
                emit downloadChanged();
                for (auto &then : std::exchange(m_afterDownload, {}))
                    then(false);
            });
    m_describing = true;
    m_download->describe();
    emit downloadChanged();
}

void AiSettings::dropDownload()
{
    m_afterDownload.clear();
    if (m_download) {
        m_download->disconnect(this);
        if (m_describing || m_installing)
            m_download->abort();
        m_download = nullptr;
    }
    m_describing = m_installing = m_installWanted = false;
    m_downloadReceived = 0;
    emit downloadChanged();
}

void AiSettings::withSidecar(std::function<void(bool)> then)
{
    prepareDownload();
    if (!m_download) {
        then(true);
        return;
    }
    m_afterDownload.push_back(std::move(then));
    m_downloadError.clear();
    if (m_installing || m_installWanted)
        return;
    if (m_describing) {
        m_installWanted = true;
    } else {
        m_installing = true;
        m_download->install();
    }
    emit downloadChanged();
}

bool AiSettings::componentNeeded()
{
    prepareDownload();
    return m_download != nullptr;
}

void AiSettings::installComponent()
{
    withSidecar([this, alive = std::weak_ptr(m_alive)](bool ready) {
        if (alive.lock())
            emit componentReady(ready);
    });
}

void AiSettings::cancelComponent()
{
    if (m_installing || m_installWanted)
        dropDownload();
}

void AiSettings::openDraft()
{
    m_probed.reset();
    setDraft({});
    m_features.setDrafting(true);
    prepareDownload();
}

void AiSettings::closeDraft()
{
    if (m_draft.value(QStringLiteral("state")) == QLatin1String("signingIn"))
        cancelSignIn();
    m_probed.reset();
    setDraft({});
    m_features.setDrafting(false);
    if (m_installing || m_installWanted)
        dropDownload();
}

void AiSettings::testDraft(const QVariantMap &form)
{
    const auto provider = draftProvider(form);
    if (!provider) {
        setDraft(QStringLiteral("failed"), QStringLiteral("form"));
        return;
    }
    const std::string key = str(form.value(QStringLiteral("key")));
    const bool editing = m_features.providers()->config().provider(provider->id) != nullptr;
    if (provider->needsKey && key.empty() && !editing) {
        setDraft(QStringLiteral("failed"), QStringLiteral("key"));
        return;
    }
    m_probed.reset();
    setDraft(QStringLiteral("testing"));
    m_features.setDrafting(true);
    withSidecar([this, alive = std::weak_ptr(m_alive), provider = *provider, key, editing](bool ready) {
        if (!alive.lock())
            return;
        if (!ready) {
            setDraft(QStringLiteral("failed"), m_downloadError);
            return;
        }
        auto *requests = m_features.requests();
        if (!requests)
            return;
        auto probe = [this, alive, provider, requests](const std::string &k) {
            requests->probe(provider, k, [this, alive, provider](backends::ai::AiRequests::Probed result) {
                if (!alive.lock())
                    return;
                if (auto *models = std::get_if<std::vector<ai::Model>>(&result)) {
                    m_probed = Probed{provider, *models};
                    setDraft(QStringLiteral("ok"), {}, 0, {}, int(models->size()));
                    return;
                }
                const auto &error = std::get<ai::Error>(result);
                setDraft(QStringLiteral("failed"), qs(ai::errorKindId(error.kind)), error.httpStatus,
                         qs(error.detail));
            });
        };
        if (!key.empty() || !provider.needsKey || !editing) {
            probe(key);
            return;
        }
        m_features.providers()->readKey(provider.id, [this, alive, probe](std::optional<std::string> stored) {
            if (!alive.lock())
                return;
            if (!stored)
                setDraft(QStringLiteral("failed"), qs(ai::errorKindId(ai::ErrorKind::Key)));
            else
                probe(*stored);
        });
    });
}

void AiSettings::saveDraft(const QVariantMap &form)
{
    auto provider = draftProvider(form);
    if (!provider) {
        setDraft(QStringLiteral("failed"), QStringLiteral("form"));
        return;
    }
    saveProvider(std::move(*provider), str(form.value(QStringLiteral("key"))));
}

void AiSettings::saveProvider(ai::Provider provider, const std::string &key)
{
    ai::Providers *providers = m_features.providers();
    if (!providers)
        return;
    const bool editing = providers->config().provider(provider.id) != nullptr;
    const bool probed = m_probed && sameEndpoint(m_probed->provider, provider);
    if (probed)
        provider.models = m_probed->models;
    setDraft(QStringLiteral("saving"));
    auto done = [this, alive = std::weak_ptr(m_alive), id = provider.id, probed](std::optional<ai::SaveError> error) {
        if (!alive.lock())
            return;
        if (error) {
            setDraft(QStringLiteral("failed"), saveErrorId(*error));
            return;
        }
        m_probed.reset();
        if (probed)
            m_status[id] = {QStringLiteral("ok"), {}, 0, {}};
        else
            m_status.erase(id);
        setDraft({});
        m_features.setDrafting(false);
        emit providersChanged();
        emit draftSaved(qs(id));
        if (!probed)
            test(qs(id));
    };
    if (editing)
        providers->update(provider, key.empty() ? std::nullopt : std::optional(key), std::move(done));
    else
        providers->add(provider, key, std::move(done));
}

void AiSettings::signInDraft(const QVariantMap &form)
{
    auto provider = draftProvider(form);
    if (!provider || provider->preset != ai::Preset::OpenRouter || !m_features.signIn()) {
        setDraft(QStringLiteral("failed"), QStringLiteral("form"));
        return;
    }
    m_probed.reset();
    setDraft(QStringLiteral("signingIn"));
    m_features.setDrafting(true);
    const auto alive = std::weak_ptr(m_alive);
    m_features.signIn()->start(
        backends::ai::SignInFlow::OpenRouter,
        [this, alive](const backends::ai::SignInProgress &progress) {
            if (alive.lock())
                signInProgress(progress);
        },
        [this, alive, provider = *provider](std::expected<backends::ai::SignedIn, backends::ai::SignInFailure> result) mutable {
            if (!alive.lock())
                return;
            m_signInPage.clear();
            if (!result) {
                if (result.error().kind == "cancelled")
                    setDraft({});
                else
                    setDraft(QStringLiteral("failed"), QStringLiteral("signIn"), 0, qs(result.error().kind));
                return;
            }
            provider.signedIn = true;
            saveProvider(std::move(provider), result->key);
        });
}

void AiSettings::signOutProvider(const QString &providerId)
{
    ai::Providers *providers = m_features.providers();
    if (!providers)
        return;
    const std::string id = providerId.toStdString();
    providers->signOut(id, [this, alive = std::weak_ptr(m_alive), id](std::optional<ai::SaveError> error) {
        if (!alive.lock())
            return;
        m_status[id] = {QStringLiteral("failed"), error ? saveErrorId(*error) : QStringLiteral("key"), 0, {}};
        emit providersChanged();
    });
}

QVariantMap AiSettings::codex() const
{
    return {{QStringLiteral("found"), m_codex.found},
            {QStringLiteral("path"), m_codex.path},
            {QStringLiteral("version"), m_codex.version},
            {QStringLiteral("canImport"), m_codex.canImport},
            {QStringLiteral("signedIn"), m_codex.signedIn},
            {QStringLiteral("email"), m_codex.email},
            {QStringLiteral("state"), m_codex.state},
            {QStringLiteral("userCode"), m_codex.userCode},
            {QStringLiteral("expiresAt"), m_codex.expiresAt},
            {QStringLiteral("browserAvailable"), m_codex.browserAvailable},
            {QStringLiteral("error"), m_codex.error},
            {QStringLiteral("host"), m_codex.host}};
}

void AiSettings::checkCodex()
{
    backends::ai::AiSignIn *signIn = m_features.signIn();
    if (!signIn)
        return;
    const AiFeatures::Ports &ports = m_features.ports();
    m_codex.canImport = !ports.codexAuthPath.isEmpty() && QFileInfo::exists(ports.codexAuthPath);
    emit codexChanged();
    signIn->codexSession([this, alive = std::weak_ptr(m_alive)](std::optional<backends::ai::CodexSession> session) {
        if (!alive.lock())
            return;
        m_codex.signedIn = session.has_value();
        m_codex.email = session ? qs(session->email) : QString();
        emit codexChanged();
        backends::ai::AiSignIn *signIn = m_features.signIn();
        if (!session || !signIn || signIn->running() || session->expiresAt == 0 ||
            session->expiresAt > QDateTime::currentMSecsSinceEpoch())
            return;
        signIn->refreshCodex([this, alive](std::expected<backends::ai::SignedIn, backends::ai::SignInFailure> result) {
            if (!alive.lock() || result || result.error().kind != "expired")
                return;
            m_codex.signedIn = false;
            setCodexFailure(result.error());
            emit codexChanged();
        });
    });
}

void AiSettings::signInCodex(bool browser)
{
    if (!m_features.signIn())
        return;
    m_codex.state = QStringLiteral("starting");
    m_codex.userCode.clear();
    m_codex.error.clear();
    m_codex.host.clear();
    emit codexChanged();
    const auto alive = std::weak_ptr(m_alive);
    m_features.signIn()->start(
        browser ? backends::ai::SignInFlow::CodexBrowser : backends::ai::SignInFlow::CodexDevice,
        [this, alive](const backends::ai::SignInProgress &progress) {
            if (!alive.lock())
                return;
            m_codex.state = qs(progress.step);
            m_codex.userCode = qs(progress.userCode);
            m_codex.expiresAt = progress.expiresAt;
            m_codex.browserAvailable = progress.browserAvailable;
            signInProgress(progress);
            emit codexChanged();
        },
        [this, alive](std::expected<backends::ai::SignedIn, backends::ai::SignInFailure> result) {
            if (!alive.lock())
                return;
            m_signInPage.clear();
            m_codex.state.clear();
            m_codex.userCode.clear();
            if (result) {
                m_codex.signedIn = true;
                m_codex.email = qs(result->email);
            } else if (result.error().kind != "cancelled") {
                setCodexFailure(result.error());
            }
            emit codexChanged();
        });
}

void AiSettings::signInProgress(const backends::ai::SignInProgress &progress)
{
    m_signInPage = progress.openUrl;
    if (progress.step == "browser" && backends::ai::AiSignIn::openable(m_signInPage))
        backends::desktop::openUrl(m_signInPage);
}

void AiSettings::setCodexFailure(const backends::ai::SignInFailure &failure)
{
    m_codex.error = qs(failure.kind);
    m_codex.host = qs(failure.host);
}

void AiSettings::cancelSignIn()
{
    if (backends::ai::AiSignIn *signIn = m_features.signIn(); signIn && signIn->running()) {
        signIn->cancel();
        return;
    }
    if (!m_codex.state.isEmpty() || !m_codex.error.isEmpty()) {
        m_codex.state.clear();
        m_codex.error.clear();
        m_codex.host.clear();
        emit codexChanged();
    }
    if (m_draft.value(QStringLiteral("state")) == QLatin1String("signingIn"))
        setDraft({});
}

void AiSettings::signOutCodex()
{
    backends::ai::AiSignIn *signIn = m_features.signIn();
    if (!signIn)
        return;
    signIn->signOutCodex([this, alive = std::weak_ptr(m_alive)](std::optional<ai::KeyError> error) {
        if (!alive.lock())
            return;
        if (error) {
            m_codex.error = QStringLiteral("keychain");
        } else {
            m_codex.signedIn = false;
            m_codex.email.clear();
            m_codex.error.clear();
        }
        emit codexChanged();
    });
}

void AiSettings::importCodex()
{
    backends::ai::AiSignIn *signIn = m_features.signIn();
    if (!signIn)
        return;
    signIn->importCodex(m_features.ports().codexAuthPath,
                        [this, alive = std::weak_ptr(m_alive)](
                            std::expected<backends::ai::SignedIn, backends::ai::SignInFailure> result) {
                            if (!alive.lock())
                                return;
                            if (result) {
                                m_codex.signedIn = true;
                                m_codex.email = qs(result->email);
                                m_codex.error.clear();
                            } else {
                                setCodexFailure(result.error());
                            }
                            emit codexChanged();
                        });
}

void AiSettings::openSignInPage() const
{
    if (backends::ai::AiSignIn::openable(m_signInPage))
        backends::desktop::openUrl(m_signInPage);
}

void AiSettings::test(const QString &providerId)
{
    const std::string id = providerId.toStdString();
    ai::Providers *providers = m_features.providers();
    if (!providers || !providers->config().provider(id))
        return;
    m_status[id] = {QStringLiteral("testing"), {}, 0, {}};
    emit providersChanged();
    withSidecar([this, alive = std::weak_ptr(m_alive), id](bool ready) {
        if (!alive.lock() || !m_features.requests())
            return;
        if (!ready) {
            m_status[id] = {QStringLiteral("failed"), m_downloadError, 0, {}};
            emit providersChanged();
            return;
        }
        m_features.requests()->refreshModels(id, [this, alive, id](std::optional<ai::Error> error) {
            if (!alive.lock())
                return;
            if (error)
                m_status[id] = {QStringLiteral("failed"), qs(ai::errorKindId(error->kind)), error->httpStatus,
                                qs(error->detail)};
            else
                m_status[id] = {QStringLiteral("ok"), {}, 0, {}};
            emit providersChanged();
        });
    });
}

void AiSettings::refreshModels()
{
    if (const ai::Providers *providers = m_features.providers())
        for (const ai::Provider &p : std::vector(providers->config().providers))
            test(qs(p.id));
}

void AiSettings::remove(const QString &providerId)
{
    ai::Providers *providers = m_features.providers();
    if (!providers)
        return;
    const std::string id = providerId.toStdString();
    providers->remove(id, [this, alive = std::weak_ptr(m_alive), id](std::optional<ai::SaveError>) {
        if (!alive.lock())
            return;
        m_status.erase(id);
        emit providersChanged();
    });
}

void AiSettings::applyRoles(const QVariantMap &roles)
{
    ai::Providers *providers = m_features.providers();
    if (!providers)
        return;
    bool changed = false;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        const auto role = ai::roleFromId(it.key().toStdString());
        if (!role)
            continue;
        const QString value = it.value().toString();
        const auto current = providers->config().roles.find(*role);
        if (value.isEmpty()) {
            if (*role != ai::Role::Chat && current != providers->config().roles.end())
                changed |= providers->unassign(*role);
            continue;
        }
        const qsizetype tab = value.indexOf(QLatin1Char('\t'));
        if (tab <= 0)
            continue;
        ai::ModelRef ref{value.left(tab).toStdString(), value.mid(tab + 1).toStdString()};
        if (current == providers->config().roles.end() || current->second != ref)
            changed |= providers->assign(*role, std::move(ref));
    }
    if (changed)
        emit providersChanged();
}

void AiSettings::setSearch(const QString &service, bool hasKey, const QString &state, const QString &error,
                           int httpStatus, bool canAddAnyway)
{
    m_search = {{QStringLiteral("service"), service}, {QStringLiteral("hasKey"), hasKey},
                {QStringLiteral("state"), state}, {QStringLiteral("error"), error},
                {QStringLiteral("httpStatus"), httpStatus}, {QStringLiteral("canAddAnyway"), canAddAnyway}};
    emit searchChanged();
}

bool AiSettings::hasSearchKey(const QString &service) const
{
    return m_search.value(QStringLiteral("service")) == service && m_search.value(QStringLiteral("hasKey")).toBool();
}

void AiSettings::checkSearchKey(const QString &service)
{
    ai::SearchKeys *keys = m_features.searchKeys();
    const auto id = ai::searchServiceFromId(service.toStdString());
    if (!keys || !id) {
        setSearch({}, false, {});
        return;
    }
    setSearch(service, false, QStringLiteral("checking"));
    keys->read(*id, [this, alive = std::weak_ptr(m_alive), service](std::optional<std::string> key) {
        if (alive.lock() && m_search.value(QStringLiteral("service")) == service)
            setSearch(service, key.has_value(), {});
    });
}

void AiSettings::saveSearchKey(const QString &service, const QString &key)
{
    if (!m_features.searchKeys() || !ai::searchServiceFromId(service.toStdString()))
        return;
    if (key.trimmed().isEmpty()) {
        setSearch(service, hasSearchKey(service), QStringLiteral("failed"), saveErrorId(ai::SaveError::Key));
        return;
    }
    runSearchTest(service, key.trimmed().toStdString());
}

void AiSettings::addSearchKeyAnyway(const QString &service, const QString &key)
{
    if (key.trimmed().isEmpty())
        return;
    const QVariantMap shown = m_search.value(QStringLiteral("service")) == service ? m_search : QVariantMap();
    const QString state = shown.value(QStringLiteral("state")).toString();
    const QString error = shown.value(QStringLiteral("error")).toString();
    const int httpStatus = shown.value(QStringLiteral("httpStatus")).toInt();
    storeSearchKey(service, key.trimmed().toStdString(), [this, service, state, error, httpStatus] {
        setSearch(service, true, state, error, httpStatus);
    });
}

void AiSettings::storeSearchKey(const QString &service, std::string key, std::function<void()> stored)
{
    ai::SearchKeys *keys = m_features.searchKeys();
    const auto id = ai::searchServiceFromId(service.toStdString());
    if (!keys || !id)
        return;
    const bool hadKey = hasSearchKey(service);
    setSearch(service, hadKey, QStringLiteral("saving"));
    keys->store(*id, std::move(key),
                [this, alive = std::weak_ptr(m_alive), service, hadKey,
                 stored = std::move(stored)](std::optional<ai::SaveError> error) {
                    if (!alive.lock() || m_search.value(QStringLiteral("service")) != service)
                        return;
                    if (error) {
                        setSearch(service, hadKey, QStringLiteral("failed"), saveErrorId(*error));
                        return;
                    }
                    m_features.applyWebSearch();
                    stored();
                    emit searchKeyStored(service);
                });
}

void AiSettings::testSearchKey(const QString &service)
{
    runSearchTest(service, std::nullopt);
}

void AiSettings::runSearchTest(const QString &service, std::optional<std::string> typedKey)
{
    const auto id = ai::searchServiceFromId(service.toStdString());
    if (!id || !m_features.requests())
        return;
    const bool hasKey = hasSearchKey(service);
    const bool typed = typedKey.has_value();
    if (!m_features.providers()->enabled()) {
        setSearch(service, hasKey, QStringLiteral("failed"), QStringLiteral("noProvider"), 0, typed);
        return;
    }
    setSearch(service, hasKey, QStringLiteral("testing"));
    withSidecar([this, alive = std::weak_ptr(m_alive), service, id = *id, hasKey,
                 typedKey = std::move(typedKey)](bool ready) mutable {
        if (!alive.lock() || !m_features.requests() || m_search.value(QStringLiteral("service")) != service)
            return;
        if (!ready) {
            setSearch(service, hasKey, QStringLiteral("failed"), m_downloadError, 0, typedKey.has_value());
            return;
        }
        m_features.requests()->testSearch(
            id, typedKey, [this, alive, service, hasKey, typedKey](std::optional<ai::Error> error) {
                if (!alive.lock() || m_search.value(QStringLiteral("service")) != service)
                    return;
                if (error) {
                    const bool keyMissing = !typedKey && error->kind == ai::ErrorKind::Key;
                    setSearch(service, hasKey && !keyMissing, QStringLiteral("failed"),
                              qs(ai::errorKindId(error->kind)), error->httpStatus, typedKey.has_value());
                } else if (typedKey) {
                    storeSearchKey(service, *typedKey, [this, service] { setSearch(service, true, QStringLiteral("ok")); });
                } else {
                    setSearch(service, true, QStringLiteral("ok"));
                }
            });
    });
}

void AiSettings::removeSearchKey(const QString &service)
{
    ai::SearchKeys *keys = m_features.searchKeys();
    const auto id = ai::searchServiceFromId(service.toStdString());
    if (!keys || !id)
        return;
    keys->remove(*id, [this, alive = std::weak_ptr(m_alive), service](std::optional<ai::SaveError> error) {
        if (!alive.lock())
            return;
        m_features.applyWebSearch();
        if (m_search.value(QStringLiteral("service")) == service)
            setSearch(service, error.has_value(), error ? QStringLiteral("failed") : QString(),
                      error ? saveErrorId(*error) : QString());
    });
}

QUrl AiSettings::searchKeysUrl(ai::SearchService service)
{
    switch (service) {
    case ai::SearchService::Brave:
        return QUrl(QStringLiteral("https://api-dashboard.search.brave.com/app/keys"));
    case ai::SearchService::Tavily:
        return QUrl(QStringLiteral("https://app.tavily.com/home"));
    case ai::SearchService::Exa:
        return QUrl(QStringLiteral("https://dashboard.exa.ai/api-keys"));
    case ai::SearchService::Firecrawl:
        return QUrl(QStringLiteral("https://www.firecrawl.dev/app/api-keys"));
    }
    return {};
}

void AiSettings::openSearchKeys(const QString &service) const
{
    if (const auto id = ai::searchServiceFromId(service.toStdString()))
        backends::desktop::openUrl(searchKeysUrl(*id));
}

void AiSettings::openWhatIsSent() const
{
    backends::desktop::openUrl(whatIsSentUrl());
}

void AiSettings::watchUpdater()
{
    backends::ai::AiSidecarUpdater *updater = m_features.updater();
    if (m_watched == updater)
        return;
    if (m_watched)
        disconnect(m_watched, nullptr, this, nullptr);
    m_watched = updater;
    m_componentState.clear();
    m_componentError.clear();
    if (!updater)
        return;
    using backends::ai::AiSidecarUpdater;
    connect(updater, &AiSidecarUpdater::progress, this, [this](qint64 received, qint64 total) {
        if (m_download)
            return;
        m_componentState = QStringLiteral("downloading");
        m_componentReceived = received;
        m_componentTotal = total;
        emit componentsChanged();
    });
    connect(updater, &AiSidecarUpdater::checked, this, [this] {
        m_componentError.clear();
        QTimer::singleShot(0, this, [this] {
            if (m_componentState == QLatin1String("checking") && (!m_watched || !m_watched->busy()))
                m_componentState.clear();
            emit componentsChanged();
        });
        emit componentsChanged();
    });
    connect(updater, &AiSidecarUpdater::downloaded, this, [this] {
        m_componentState.clear();
        emit componentsChanged();
    });
    connect(updater, &AiSidecarUpdater::failed, this, [this](backends::ai::SidecarDownloadError error, const QString &) {
        if (m_componentState == QLatin1String("cancelled"))
            m_componentError.clear();
        else
            m_componentError = downloadErrorId(error);
        m_componentState.clear();
        emit componentsChanged();
    });
}

QVariantMap AiSettings::components() const
{
    const backends::ai::AiSidecarUpdater *updater = m_features.updater();
    if (!updater)
        return {{QStringLiteral("present"), false}};
    const QJsonObject versions = m_features.versionProperties();
    const auto staged = updater->staged();
    QVariantMap rollback;
    if (const auto r = updater->rollback())
        rollback = {{QStringLiteral("version"), r->version},
                    {QStringLiteral("to"), r->to},
                    {QStringLiteral("reportSent"), r->reportSent}};
    const QString state = m_componentState == QLatin1String("cancelled") ? QString() : m_componentState;
    return {{QStringLiteral("present"), true},
            {QStringLiteral("version"), versions.value(QLatin1String("ai_component_version")).toString()},
            {QStringLiteral("runtime"), versions.value(QLatin1String("ai_runtime_version")).toString()},
            {QStringLiteral("anthropic"), versions.value(QLatin1String("ai_anthropic_version")).toString()},
            {QStringLiteral("openai"), versions.value(QLatin1String("ai_openai_version")).toString()},
            {QStringLiteral("google"), versions.value(QLatin1String("ai_google_version")).toString()},
            {QStringLiteral("available"), updater->available()},
            {QStringLiteral("availableBytes"), updater->availableBytes()},
            {QStringLiteral("staged"), staged ? staged->version : QString()},
            {QStringLiteral("newerNeedsApp"), updater->newerNeedsApp()},
            {QStringLiteral("checkedAt"), updater->lastCheck().isValid() ? updater->lastCheck().toMSecsSinceEpoch() : 0},
            {QStringLiteral("state"), state},
            {QStringLiteral("received"), m_componentReceived},
            {QStringLiteral("total"), m_componentTotal},
            {QStringLiteral("error"), m_componentError},
            {QStringLiteral("policyOff"), m_features.autoUpdateOffByPolicy()},
            {QStringLiteral("rollback"), rollback}};
}

void AiSettings::checkComponents()
{
    watchUpdater();
    if (!m_watched || m_watched->busy())
        return;
    m_componentState = QStringLiteral("checking");
    m_componentError.clear();
    m_componentReceived = m_componentTotal = 0;
    m_features.checkNow();
    emit componentsChanged();
}

void AiSettings::updateComponents()
{
    watchUpdater();
    if (!m_watched || m_watched->busy())
        return;
    m_componentState = QStringLiteral("checking");
    m_componentError.clear();
    m_componentReceived = 0;
    m_componentTotal = m_watched->availableBytes();
    m_features.updateNow();
    emit componentsChanged();
}

void AiSettings::cancelComponents()
{
    if (!m_watched || !m_watched->busy())
        return;
    m_componentState = QStringLiteral("cancelled");
    m_watched->abort();
    emit componentsChanged();
}

void AiSettings::dismissComponentNotice()
{
    if (backends::ai::AiSidecarUpdater *updater = m_features.updater()) {
        updater->setRollback(std::nullopt);
        emit componentsChanged();
    }
}

void AiSettings::openWhatsNew() const
{
    backends::desktop::openUrl(whatsNewUrl());
}

QVariantMap AiSettings::agents() const
{
    QVariantMap out;
    for (const auto &[name, agent] : m_agents)
        out.insert(name, agentRow(name));
    return out;
}

QVariantMap AiSettings::agentRow(const QString &name) const
{
    const Agent &a = *m_agents.at(name);
    if (!a.cli)
        return {{QStringLiteral("found"), false}, {QStringLiteral("missing"), a.missing}};
    const QString refused = m_features.refusal(name);
    backends::ai::AgentCompat compat;
    if (const backends::ai::AiSidecarUpdater *updater = m_features.updater())
        if (const auto it = updater->agentCompat().find(name); it != updater->agentCompat().end())
            compat = it->second;
    if (name == QLatin1String("agy"))
        compat.minimum = QLatin1String(backends::ai::kAntigravityMinimumVersion);
    const bool updating = a.update && a.update->running();
    return {{QStringLiteral("found"), true},
            {QStringLiteral("refused"), refused},
            {QStringLiteral("lowRights"), m_features.runsWithLowRights(name)},
            {QStringLiteral("path"), shownPath(a.cli->path)},
            {QStringLiteral("version"), a.cli->version},
            {QStringLiteral("latest"), a.latest},
            {QStringLiteral("badge"), badgeId(backends::ai::agentBadge(a.cli->version, a.latest, compat))},
            {QStringLiteral("method"), methodId(a.install.method)},
            {QStringLiteral("minimum"), compat.minimum},
            {QStringLiteral("canUpdate"), !a.install.program.isEmpty()},
            {QStringLiteral("command"), a.install.manualCommand},
            {QStringLiteral("session"), a.session},
            {QStringLiteral("state"), updating ? QStringLiteral("updating") : QString()},
            {QStringLiteral("outcome"), a.outcome},
            {QStringLiteral("lastLine"), a.lastLine},
            {QStringLiteral("acp"), isAcpCli(name)}};
}

void AiSettings::checkAgents()
{
    const AiFeatures::Ports &ports = m_features.ports();
    refreshAgentSessions();
    m_acp->check();
    if (!ports.findAgent)
        return;
    for (const auto &[name, agent] : m_agents) {
        if (agent->update && agent->update->running())
            continue;
        ports.findAgent(name, [this, alive = std::weak_ptr(m_alive), name](std::optional<backends::ai::AgentCli> cli,
                                                                           QString unresolvedShim) {
            if (!alive.lock())
                return;
            Agent &a = *m_agents.at(name);
            a.cli = std::move(cli);
            a.missing = unresolvedShimText(unresolvedShim);
            if (isAcpCli(name)) {
                a.install = acpInstall(name);
                return emit agentsChanged();
            }
            a.install = a.cli && m_features.ports().agentInstall ? m_features.ports().agentInstall(*a.cli)
                                                                  : backends::ai::AgentInstall{};
            if (name == QLatin1String("codex")) {
                m_codex.found = a.cli.has_value();
                m_codex.path = a.cli ? a.cli->path : QString();
                m_codex.version = a.cli ? a.cli->version : QString();
                emit codexChanged();
            }
            emit agentsChanged();
            if (!a.cli || !m_features.ports().agentLatest)
                return;
            m_features.ports().agentLatest(a.install, [this, alive, name](QString latest) {
                if (!alive.lock())
                    return;
                m_agents.at(name)->latest = std::move(latest);
                emit agentsChanged();
            });
        });
    }
}

void AiSettings::updateAgent(const QString &agent)
{
    const auto it = m_agents.find(agent);
    if (it == m_agents.end())
        return;
    Agent &a = *it->second;
    refreshAgentSessions();
    if (!a.cli || a.install.program.isEmpty() || a.session || (a.update && a.update->running()))
        return;
    a.update = std::make_unique<backends::ai::AgentUpdate>();
    a.outcome.clear();
    a.lastLine.clear();
    connect(a.update.get(), &backends::ai::AgentUpdate::finished, this,
            [this, agent](backends::ai::AgentUpdate::Outcome outcome, const QString &version, const QString &lastLine) {
                Agent &done = *m_agents.at(agent);
                done.outcome = outcomeId(outcome);
                done.lastLine = lastLine;
                if (outcome == backends::ai::AgentUpdate::Outcome::Updated && done.cli)
                    done.cli->version = version;
                emit agentsChanged();
            });
    a.update->start(a.install, a.cli->path, a.cli->version);
    emit agentsChanged();
}

void AiSettings::cancelAgentUpdate(const QString &agent)
{
    if (const auto it = m_agents.find(agent); it != m_agents.end() && it->second->update)
        it->second->update->cancel();
}

void AiSettings::refreshAgentSessions()
{
    bool changed = false;
    for (const auto &[name, agent] : m_agents) {
        const bool session = m_features.agentSessions(name.toStdString()) > 0;
        changed = changed || session != agent->session;
        agent->session = session;
    }
    if (changed)
        emit agentsChanged();
}

void AiSettings::openGeminiKeys() const
{
    backends::desktop::openUrl(geminiKeysUrl());
}

void AiSettings::openOpenRouterKeys() const
{
    backends::desktop::openUrl(openRouterKeysUrl());
}

void AiSettings::reset()
{
    m_codex = {};
    m_signInPage.clear();
    emit codexChanged();
    m_status.clear();
    m_search.clear();
    emit searchChanged();
    m_probed.reset();
    dropDownload();
    m_downloadSize = 0;
    m_downloadError.clear();
    setDraft({});
}

}
