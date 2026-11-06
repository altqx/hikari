#include "hikari/app/ai_settings.h"

#include "hikari/app/ai_features.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_download.h"
#include "hikari/backends/ai_signin.h"
#include "hikari/backends/desktop_handoff.h"

#include <QDateTime>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

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

bool sameEndpoint(const ai::Provider &a, const ai::Provider &b)
{
    return a.id == b.id && a.preset == b.preset && a.wire == b.wire && a.baseUrl == b.baseUrl;
}

}

AiSettings::AiSettings(AiFeatures &features, QObject *parent) : QObject(parent), m_features(features)
{
    connect(&m_features, &AiFeatures::switched, this, [this] {
        reset();
        emit switched();
        emit providersChanged();
    });
}

AiSettings::~AiSettings()
{
    *m_alive = false;
}

bool AiSettings::sandboxed() const
{
    return m_features.sandboxed();
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
        std::string fresh(ai::presetId(*preset));
        for (int n = 2; providers->config().provider(fresh); ++n)
            fresh = std::string(ai::presetId(*preset)) + '-' + std::to_string(n);
        p = ai::presetProvider(*preset, fresh, {});
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
    m_download = m_features.sidecarDownload();
    if (!m_download)
        return;
    using backends::ai::AiSidecarDownload;
    connect(m_download.get(), &AiSidecarDownload::described, this, [this](const backends::ai::SidecarAsset &asset) {
        m_describing = false;
        m_downloadSize = asset.size;
        if (m_installWanted) {
            m_installWanted = false;
            m_installing = true;
            m_download->install();
        }
        emit downloadChanged();
    });
    connect(m_download.get(), &AiSidecarDownload::progress, this, [this](qint64 received, qint64 total) {
        m_downloadReceived = received;
        if (total > 0)
            m_downloadSize = total;
        emit downloadChanged();
    });
    connect(m_download.get(), &AiSidecarDownload::installed, this, [this](const QString &) {
        auto waiting = std::exchange(m_afterDownload, {});
        m_download.release()->deleteLater();
        m_installing = false;
        m_downloadError.clear();
        emit downloadChanged();
        for (auto &then : waiting)
            then(true);
    });
    connect(m_download.get(), &AiSidecarDownload::failed, this,
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
        m_download->abort();
        m_download.release()->deleteLater();
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
    if (ports.findCodex)
        ports.findCodex([this, alive = std::weak_ptr(m_alive)](std::optional<backends::ai::CodexCli> cli) {
            if (!alive.lock())
                return;
            m_codex.found = cli.has_value();
            m_codex.path = cli ? cli->path : QString();
            m_codex.version = cli ? cli->version : QString();
            emit codexChanged();
        });
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

void AiSettings::openWhatIsSent() const
{
    backends::desktop::openUrl(whatIsSentUrl());
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
    m_probed.reset();
    dropDownload();
    m_downloadSize = 0;
    m_downloadError.clear();
    setDraft({});
}

}
