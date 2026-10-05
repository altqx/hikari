#pragma once

// Y8: the "Font collector" dialog's state (legacy FontCollectorDialog and
// FontCollector, FontCollector.cpp at 20d647c4): the Options radio box,
// the path with "Select a folder", "Save to video / subtitles folder.",
// Start and "Start on tabs", the found / not found log, "Save folder" and
// the FONT_COLLECTOR_* settings. The work runs on a thread as legacy's
// FontCollectorThread did. The copy modes use the staged review (routing
// #60): Start prepares and logs what would be written, Apply writes it; an
// incomplete review is written only after acknowledgment and then labelled
// (routing #54). "Demux fonts from loaded MKV file" belongs to Y9.

#include "hikari/application/font_collector.h"

#include <QDir>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class FontCollectorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int action READ action NOTIFY settingsChanged)              // FONT_COLLECTOR_ACTION
    Q_PROPERTY(QString directory READ directory NOTIFY settingsChanged)    // FONT_COLLECTOR_DIRECTORY
    Q_PROPERTY(bool useSubsDirectory READ useSubsDirectory NOTIFY settingsChanged)
    // The log as {text, kind} segments: kind 0 normal, 1 warning, 2 success
    // (legacy fcd->normal, fcd->warning and "#008000").
    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(int stage READ stage NOTIFY stageChanged)
    Q_PROPERTY(bool reviewIncomplete READ reviewIncomplete NOTIFY stageChanged)
    Q_PROPERTY(bool canSaveFolder READ canSaveFolder NOTIFY stageChanged)
    Q_PROPERTY(QString copyPath READ copyPath NOTIFY stageChanged)
public:
    enum Stage { Options = 0, Working = 1, Review = 2, Done = 3 };
    Q_ENUM(Stage)

    struct Tab {
        std::shared_ptr<const core::Document> document; // a copy for the run
        QString path;                                  // SubsPath; empty when unsaved
    };
    struct Hooks {
        std::function<std::vector<Tab>()> tabs; // every tab, in order
        std::function<int()> currentTab;
        // FontCollectorDialog::SetLine and OpenStyle: the tab, then the Line
        // (the first Line of the Style), as ChangeActiveLine does.
        std::function<void(int tab, int line)> goToLine;
        std::function<void(int tab, const QString &style)> goToStyle;
        // SelectInFolder; null: the platform's file manager.
        std::function<void(const QString &path)> reveal;
    };

    FontCollectorController(ui::SettingsStore &settings, Hooks hooks, QObject *parent = nullptr);
    ~FontCollectorController() override;

    int action() const { return m_action; }
    QString directory() const { return m_directory; }
    bool useSubsDirectory() const { return m_useSubsDirectory; }
    QVariantList log() const;
    QString logText() const;
    int stage() const { return m_stage; }
    bool reviewIncomplete() const { return m_review && !m_review->complete(); }
    bool canSaveFolder() const { return m_canSaveFolder; }
    QString copyPath() const { return m_copyPath; }

    // ShowDialog: a new dialog with the settings' values, an empty log and
    // "Save folder" disabled.
    Q_INVOKABLE void open();
    // OnChangeOpt: the Options choice and the checkbox are saved.
    Q_INVOKABLE void changeOptions(int action, bool useSubsDirectory);
    // OnButtonPath: where the folder or archive chooser starts ({folder,
    // name}), then what it returned. A cancelled chooser's empty answer
    // keeps the previous path (FC-chooser-cancel; legacy stored it).
    Q_INVOKABLE QVariantMap chooserStart(const QString &path) const;
    Q_INVOKABLE void chooseDirectory(const QString &path);
    // OnButtonStart. Returns {message} when legacy refused with a message
    // box, {question, archive} for "The zip file already exists, delete
    // it?" (answer with confirmReplace), else {} and the run started. A Yes
    // removes the archive only when Apply starts, not before the review.
    Q_INVOKABLE QVariantMap start(const QString &path, bool allTabs);
    Q_INVOKABLE bool confirmReplace(bool remove);
    // The staged review's Apply; `acknowledged` is the incomplete-output
    // acknowledgment.
    Q_INVOKABLE void apply(bool acknowledged);
    Q_INVOKABLE void cancel();
    // Close: a review not applied is dropped, a running job cancelled.
    Q_INVOKABLE void close();
    // "Save folder": SelectInFolder(copypath).
    Q_INVOKABLE void saveFolder();
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    Q_INVOKABLE QString localPath(const QUrl &url) const { return QDir::toNativeSeparators(url.toLocalFile()); }
    // OnConsoleDoubleClick at a position of logText.
    Q_INVOKABLE void logDoubleClicked(int position);

    // W2: the renderer the video's subtitles go through ("libass", or a
    // CSRI renderer's name on Windows). The check verifies libass only
    // (fonts.md: CSRI/VSFilter output needs its own agreement evidence), so
    // with another renderer the log says its output was not verified.
    void setDisplayRenderer(const QString &name) { m_displayRenderer = name; }
    QString displayRenderer() const { return m_displayRenderer; }

    // Tests: another font service, and waiting for the worker.
    void setFontService(std::unique_ptr<application::FontServicePort> service);
    bool waitIdle(int ms = 30000);
    void setCloseWait(int ms) { m_closeWaitMs = ms; } // how long close() waits for a running job

signals:
    void settingsChanged();
    void logChanged();
    void stageChanged();
    void styleRequested(const QString &style); // StyleStore::ShowStyleEdit

private:
    struct Segment {
        QString text;
        int kind = 0;
    };
    struct Area {
        int stylesFrom = -1, stylesTo = -1, linesFrom = -1, linesTo = -1;
        application::collector::Block block;
    };
    void send(const QString &text, int kind);
    void clearLog();
    void doLog(const application::collector::Block &block);
    void logBlocks(const std::map<std::u16string, application::collector::Block> &found,
                   const std::map<std::u16string, application::collector::Block> &notFound);
    void logRenderer(const application::CollectorReview &review);
    void logSummary(const application::CollectorReview &review, const application::CollectorResult &result);
    void logFinished();
    void runPrepare();
    void prepared(std::expected<application::CollectorReview, application::FontError> review);
    void applied(application::CollectorResult result);
    void join();
    void setStage(Stage stage);

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    std::unique_ptr<application::FontServicePort> m_service;
    std::unique_ptr<application::FontCollector> m_collector;
    int m_action = 0;
    QString m_directory;
    bool m_useSubsDirectory = false;
    QString m_displayRenderer = QStringLiteral("libass"); // W2
    std::vector<Segment> m_log;
    std::vector<Area> m_areas;
    int m_stage = Options;
    bool m_canSaveFolder = false;
    QString m_copyPath;
    QString m_pendingArchive;
    QString m_removeArchive; // answered Yes; removed when Apply starts
    std::vector<application::CollectorDocument> m_documents;
    application::CollectorAction m_runAction = application::CollectorAction::Check;
    std::optional<application::CollectorReview> m_review;
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
    QElapsedTimer m_clock;
    qint64 m_elapsed = 0;
    int m_closeWaitMs = 30000;
};

} // namespace hikari::app
