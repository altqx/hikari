#pragma once

#include "hikari/application/ai_inline.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/write_coordinator.h"
#include "hikari/backends/ai_translation_run.h"
#include "hikari/core/document.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::api {
class Api;
}
namespace hikari::application {
class AiReview;
class DocumentFiles;
class Workspace;
}
namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class AgentController;
class AiFeatures;

/// AI inline actions: one prompt per selected Line, from the Grid or the Line editor. In Auto and Full
/// access a run is written once, when it ends, as one review turn; in Manual its results wait for Apply.
class InlineAiController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool on READ on NOTIFY changed)
    Q_PROPERTY(QVariantMap ready READ ready NOTIFY changed)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(QString translateTo READ translateTo NOTIFY changed)
    Q_PROPERTY(QStringList recentInstructions READ recentInstructions NOTIFY changed)
    Q_PROPERTY(QVariantMap last READ last NOTIFY changed)
    Q_PROPERTY(int maxCps READ maxCps CONSTANT)
    Q_PROPERTY(QVariantMap progress READ progress NOTIFY runChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY runChanged)
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY runChanged)

public:
    /// The window's `translation.translator` and `translation.reviewer`, "<provider id>/<model>"; empty uses the role's
    /// model. `waitingForTrust`: an untrusted Project sets one, so the user's models run instead.
    struct ProjectModels {
        QString translator;
        QString reviewer;
        bool waitingForTrust = false;
    };

    struct Wiring {
        AiFeatures &ai;
        ui::SettingsStore &settings;
        api::Api &api;
        application::AiReview &review;
        application::Workspace &workspace;
        application::DocumentFiles &files;
        AgentController &agent;
        std::function<void(application::DocumentId, core::LineId)> select;
        std::function<ProjectModels(application::DocumentId)> projectModels = {};
        /// The Project folder of the Document's window; empty without one.
        std::function<QString(application::DocumentId)> projectFolder = {};
        /// The active window's `translation.targetLanguage`; empty uses `ai.inline.translateTo`.
        std::function<QString()> targetLanguage = {};
        /// The Document's window's `translation.sourceLanguage`; empty lets the models detect it.
        std::function<QString(application::DocumentId)> sourceLanguage = {};
    };

    static constexpr const char *kActionsSetting = "ai.inline.actions";
    static constexpr const char *kTranslateToSetting = "ai.inline.translateTo";
    static constexpr const char *kContextLinesSetting = "ai.inline.contextLines";
    static constexpr const char *kRecentSetting = "ai.inline.recentInstructions";
    static constexpr const char *kCallerId = "ai:inline";
    static constexpr int kConfirmOver = 50;

    explicit InlineAiController(Wiring wiring, QObject *parent = nullptr);
    ~InlineAiController() override;

    bool on() const;
    /// {chat, translator, reviewer, any}: a provider model serves the role. An agent CLI doesn't count.
    QVariantMap ready() const;
    QVariantList actions() const;
    QString translateTo() const;
    void projectChanged();
    QStringList recentInstructions() const;
    QVariantMap last() const;
    int maxCps() const;
    QVariantMap progress() const;
    QVariantList results() const;
    QVariantMap notice() const;

    /// `ask`: {action: "rewrite" | "shorten" | "spelling" | "translate" | "custom", instruction?, language?, custom?
    /// (an index into actions)}. `editor`: the Line editor's active Line instead of the Grid's selection.
    /// Answers {send, skipped, role, model, ready, estimate (dollars, or -1 unknown), confirm, dailyLeft}.
    Q_INVOKABLE QVariantMap plan(const QVariantMap &ask, bool editor) const;
    /// Starts the run; false when nothing would be sent or a run is going. A span in `fieldText` limits the change
    /// to that part when the field shows the Line's text as stored.
    Q_INVOKABLE bool start(const QVariantMap &ask, bool editor, int spanStart = -1, int spanEnd = -1,
                           const QString &fieldText = {});
    Q_INVOKABLE bool repeatLast(bool editor);
    Q_INVOKABLE void stop();
    /// Manual: writes the results with these keys as one review turn.
    Q_INVOKABLE bool apply(const QVariantList &keys);
    Q_INVOKABLE void discard();
    /// Runs the Lines that failed again, with the same action.
    Q_INVOKABLE bool retry();
    Q_INVOKABLE void dismiss();
    /// Selects the result's Line in the Grid.
    Q_INVOKABLE void show(qulonglong key);

    /// [{name, instruction, role: "chat" | "translator" | "reviewer", ask: bool}], saved at once.
    Q_INVOKABLE void setActions(const QVariantList &actions);

    /// After any Document change: Lines edited by hand leave the run.
    void refreshed();

signals:
    void changed();
    void runChanged();
    /// "done", "stopped", "dailyCap" or "noModel".
    void finished(QString end);

private:
    struct Line;
    struct Run;

    std::optional<application::ai_inline::Ask> askOf(const QVariantMap &ask, std::string *label,
                                                     application::ai::Role *role) const;
    struct Picked {
        application::DocumentId document;
        std::vector<core::LineId> send;
        std::size_t skipped = 0;
    };
    std::optional<Picked> pick(const application::ai_inline::Ask &ask, bool editor) const;
    std::optional<Picked> pick(const application::ai_inline::Ask &ask, application::DocumentId document,
                               std::vector<core::LineId> lines) const;
    bool launch(const QVariantMap &askMap, Picked picked, std::optional<std::pair<std::size_t, std::size_t>> span);
    std::unique_ptr<backends::ai::LineRun> translationRun(std::vector<backends::ai::TranslationRun::Job> jobs,
                                                          backends::ai::InlineRun::Wiring lines,
                                                          const application::ai_inline::Ask &ask);
    void writeProjectFiles();
    bool write(const std::vector<std::uint64_t> &keys);
    const core::Document *documentOf(application::DocumentId document) const;
    std::string mode() const;
    void dropEdited();
    void closeResults();

    Wiring m_wiring;
    std::unique_ptr<Run> m_run;
    QVariantMap m_lastAsk;
};

}
