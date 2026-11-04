#include "hikari/app/ai_settings.h"

#include "hikari/app/ai_features.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_download.h"
#include "hikari/backends/desktop_handoff.h"

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
            {QStringLiteral("needsKey"), p->needsKey}};
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
    ai::Providers *providers = m_features.providers();
    const std::string key = str(form.value(QStringLiteral("key")));
    const bool editing = providers->config().provider(provider->id) != nullptr;
    const bool probed = m_probed && sameEndpoint(m_probed->provider, *provider);
    if (probed)
        provider->models = m_probed->models;
    setDraft(QStringLiteral("saving"));
    auto done = [this, alive = std::weak_ptr(m_alive), id = provider->id, probed](std::optional<ai::SaveError> error) {
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
        providers->update(*provider, key.empty() ? std::nullopt : std::optional(key), std::move(done));
    else
        providers->add(*provider, key, std::move(done));
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

void AiSettings::reset()
{
    m_status.clear();
    m_probed.reset();
    dropDownload();
    m_downloadSize = 0;
    m_downloadError.clear();
    setDraft({});
}

}
