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
}

namespace hikari::app {

class AiFeatures;

/// Settings › AI: the providers, their keys and catalogues, and the role models. Providers and keys are saved at
/// once; role models wait for OK or Apply (applyRoles).
class AiSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool on READ on NOTIFY switched)
    /// {id, name, preset, wire, baseUrl, needsKey, models, state, error, httpStatus, detail}; state is "", "testing",
    /// "ok" or "failed".
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    /// {value: "<provider id>\t<model id>", text, provider, vision}
    Q_PROPERTY(QVariantList modelChoices READ modelChoices NOTIFY providersChanged)
    /// Role id to its own model's value, or "" when it uses the chat model.
    Q_PROPERTY(QVariantMap roles READ roles NOTIFY providersChanged)
    /// The add or edit form: {state, error, httpStatus, detail, models}; state is "", "testing", "saving", "ok" or
    /// "failed".
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
    /// {needed, size, received, active, error}: the AI component's one-time download.
    Q_PROPERTY(QVariantMap download READ download NOTIFY downloadChanged)

public:
    explicit AiSettings(AiFeatures &features, QObject *parent = nullptr);
    ~AiSettings() override;

    bool on() const;
    QVariantList providers() const;
    QVariantList modelChoices() const;
    QVariantMap roles() const;
    QVariantMap draft() const { return m_draft; }
    QVariantMap download() const;

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
    Q_INVOKABLE void openWhatIsSent() const;
    Q_INVOKABLE void openGeminiKeys() const;

    static QUrl whatIsSentUrl() { return QUrl(QStringLiteral("https://hikarisub.org/en/docs/help/what-is-sent#ai")); }
    static QUrl geminiKeysUrl() { return QUrl(QStringLiteral("https://aistudio.google.com/apikey")); }

signals:
    void switched();
    void providersChanged();
    void draftChanged();
    void downloadChanged();
    void draftSaved(const QString &providerId);

private:
    struct Status {
        QString state;
        QString error;
        int httpStatus = 0;
        QString detail;
    };

    std::optional<application::ai::Provider> draftProvider(const QVariantMap &form) const;
    void setDraft(const QString &state, const QString &error = {}, int httpStatus = 0, const QString &detail = {},
                  int models = 0);
    void withSidecar(std::function<void(bool ready)> then);
    void prepareDownload();
    void dropDownload();
    void reset();

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
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
