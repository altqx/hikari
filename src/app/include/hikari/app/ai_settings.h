#pragma once

#include "hikari/application/ai_providers.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::backends::ai {
class AiSidecarDownload;
struct SignInFailure;
struct SignInProgress;
}

namespace hikari::app {

class AiFeatures;

/// Settings › AI: the providers, their keys and catalogues, and the role models. Providers and keys are saved at
/// once; role models wait for OK or Apply (applyRoles).
class AiSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool on READ on NOTIFY switched)
    /// {id, name, preset, wire, baseUrl, needsKey, signedIn, models, state, error, httpStatus, detail}; state is "",
    /// "testing", "ok" or "failed".
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    /// {value: "<provider id>\t<model id>", text, provider, vision}
    Q_PROPERTY(QVariantList modelChoices READ modelChoices NOTIFY providersChanged)
    /// Role id to its own model's value, or "" when it uses the chat model.
    Q_PROPERTY(QVariantMap roles READ roles NOTIFY providersChanged)
    /// The add or edit form: {state, error, httpStatus, detail, models}; state is "", "testing", "signingIn",
    /// "saving", "ok" or "failed". A failed sign-in has error "signIn" and the sign-in's kind as detail.
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
    /// {needed, size, received, active, error}: the AI component's one-time download.
    Q_PROPERTY(QVariantMap download READ download NOTIFY downloadChanged)
    /// Codex under External agents: {found, path, version, canImport, signedIn, email, state, userCode, expiresAt,
    /// browserAvailable, error, host}. state is "", "starting", "code" or "browser"; error is a sign-in failure's kind.
    Q_PROPERTY(QVariantMap codex READ codex NOTIFY codexChanged)
    /// False where the OS sandbox can't apply, so the page says the AI component isn't sandboxed.
    Q_PROPERTY(bool sandboxed READ sandboxed NOTIFY switched)

public:
    explicit AiSettings(AiFeatures &features, QObject *parent = nullptr);
    ~AiSettings() override;

    bool on() const;
    QVariantList providers() const;
    QVariantList modelChoices() const;
    QVariantMap roles() const;
    QVariantMap draft() const { return m_draft; }
    QVariantMap download() const;
    QVariantMap codex() const;
    bool sandboxed() const;

    /// {id, preset, name, baseUrl, wire, needsKey} of an added provider, for its edit form.
    Q_INVOKABLE QVariantMap form(const QString &providerId) const;
    /// The add or edit form opened: reads what the download would be, without downloading.
    Q_INVOKABLE void openDraft();
    Q_INVOKABLE void closeDraft();
    /// form: {id (empty for a new one), preset, name, baseUrl, wire, key (empty keeps an edited one's key)}.
    Q_INVOKABLE void testDraft(const QVariantMap &form);
    Q_INVOKABLE void saveDraft(const QVariantMap &form);
    Q_INVOKABLE void test(const QString &providerId);
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void remove(const QString &providerId);
    Q_INVOKABLE void applyRoles(const QVariantMap &roles);
    /// Settings › AI opened: looks for the Codex CLI and HikariSub's ChatGPT sign-in, refreshing an expired one.
    Q_INVOKABLE void checkCodex();
    /// A device code, or the browser on localhost:1455.
    Q_INVOKABLE void signInCodex(bool browser);
    /// Ends the Codex or OpenRouter sign-in that is running, or dismisses the Codex sign-in's failure.
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void signOutCodex();
    /// Asked first: the two then share one sign-in.
    Q_INVOKABLE void importCodex();
    /// The running sign-in's page, again.
    Q_INVOKABLE void openSignInPage() const;
    /// Signs in to OpenRouter for the form's new provider, then adds and tests it.
    Q_INVOKABLE void signInDraft(const QVariantMap &form);
    /// Deletes a signed-in provider's key; it stays, keyless, until the user signs in again or pastes a key.
    Q_INVOKABLE void signOutProvider(const QString &providerId);
    Q_INVOKABLE void openWhatIsSent() const;
    Q_INVOKABLE void openGeminiKeys() const;
    Q_INVOKABLE void openOpenRouterKeys() const;

    static QUrl whatIsSentUrl() { return QUrl(QStringLiteral("https://hikarisub.org/en/docs/help/what-is-sent#ai")); }
    static QUrl geminiKeysUrl() { return QUrl(QStringLiteral("https://aistudio.google.com/apikey")); }
    static QUrl openRouterKeysUrl() { return QUrl(QStringLiteral("https://openrouter.ai/settings/keys")); }

signals:
    void switched();
    void providersChanged();
    void draftChanged();
    void downloadChanged();
    void codexChanged();
    void draftSaved(const QString &providerId);

private:
    struct Status {
        QString state;
        QString error;
        int httpStatus = 0;
        QString detail;
    };

    std::optional<application::ai::Provider> draftProvider(const QVariantMap &form) const;
    void saveProvider(application::ai::Provider provider, const std::string &key);
    void setDraft(const QString &state, const QString &error = {}, int httpStatus = 0, const QString &detail = {},
                  int models = 0);
    void withSidecar(std::function<void(bool ready)> then);
    void prepareDownload();
    void dropDownload();
    void reset();
    void signInProgress(const backends::ai::SignInProgress &progress);
    void setCodexFailure(const backends::ai::SignInFailure &failure);

    AiFeatures &m_features;
    std::map<std::string, Status, std::less<>> m_status;
    QVariantMap m_draft;
    struct Probed {
        application::ai::Provider provider;
        std::vector<application::ai::Model> models;
    };
    std::optional<Probed> m_probed;
    std::unique_ptr<backends::ai::AiSidecarDownload> m_download;
    bool m_describing = false;
    bool m_installing = false;
    bool m_installWanted = false;
    qint64 m_downloadSize = 0;
    qint64 m_downloadReceived = 0;
    QString m_downloadError;
    std::vector<std::function<void(bool)>> m_afterDownload;
    struct Codex {
        bool found = false;
        QString path;
        QString version;
        bool canImport = false;
        bool signedIn = false;
        QString email;
        QString state;
        QString userCode;
        qint64 expiresAt = 0;
        bool browserAvailable = true;
        QString error;
        QString host;
    };
    Codex m_codex;
    QUrl m_signInPage;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
