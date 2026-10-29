#pragma once

#include "hikari/application/font_collector.h"
#include "hikari/application/matroska.h"

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
    Q_PROPERTY(int action READ action NOTIFY settingsChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY settingsChanged)
    Q_PROPERTY(bool useSubsDirectory READ useSubsDirectory NOTIFY settingsChanged)
    // {text, kind} segments; kind 0 normal, 1 warning, 2 success.
    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(int stage READ stage NOTIFY stageChanged)
    Q_PROPERTY(bool reviewIncomplete READ reviewIncomplete NOTIFY stageChanged)
    Q_PROPERTY(bool canSaveFolder READ canSaveFolder NOTIFY stageChanged)
    Q_PROPERTY(QString copyPath READ copyPath NOTIFY stageChanged)
    Q_PROPERTY(bool fromMkv READ fromMkv NOTIFY settingsChanged)
    Q_PROPERTY(bool fromMkvEnabled READ fromMkvEnabled NOTIFY settingsChanged)
public:
    enum Stage { Options = 0, Working = 1, Review = 2, Done = 3 };
    Q_ENUM(Stage)

    struct Tab {
        std::shared_ptr<const core::Document> document;
        // Empty when unsaved.
        QString path;
        // Empty without a video.
        QString video;
    };
    struct Hooks {
        std::function<std::vector<Tab>()> tabs;
        std::function<int()> currentTab;
        std::function<void(int tab, int line)> goToLine;
        std::function<void(int tab, const QString &style)> goToStyle;
        // Null: the platform's file manager.
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
    bool fromMkv() const { return m_fromMkv; }
    bool fromMkvEnabled() const { return m_fromMkvEnabled; }

    Q_INVOKABLE void open();
    Q_INVOKABLE void changeOptions(int action, bool useSubsDirectory);
    Q_INVOKABLE void setFromMkv(bool on);
    // Returns {folder, name} for the chooser's start.
    Q_INVOKABLE QVariantMap chooserStart(const QString &path) const;
    // An empty path keeps the previous one.
    Q_INVOKABLE void chooseDirectory(const QString &path);
    // Returns {message} when refused, {question, title, archive} when the archive exists
    // (answer with confirmReplace), else {} and the run started. A Yes removes the archive
    // only when Apply starts.
    Q_INVOKABLE QVariantMap start(const QString &path, bool allTabs);
    Q_INVOKABLE bool confirmReplace(bool remove);
    // `acknowledged`: the user accepted an incomplete review.
    Q_INVOKABLE void apply(bool acknowledged);
    Q_INVOKABLE void cancel();
    // Drops a review not applied and cancels a running job.
    Q_INVOKABLE void close();
    Q_INVOKABLE void saveFolder();
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    Q_INVOKABLE QString localPath(const QUrl &url) const { return QDir::toNativeSeparators(url.toLocalFile()); }
    // `position` is an offset into logText.
    Q_INVOKABLE void logDoubleClicked(int position);

    // "libass" or a CSRI renderer's name; anything but libass is logged as unverified.
    void setDisplayRenderer(const QString &name) { m_displayRenderer = name; }
    QString displayRenderer() const { return m_displayRenderer; }

    void setFontService(std::unique_ptr<application::FontServicePort> service);
    void setMatroskaPort(std::unique_ptr<application::MatroskaPort> port) { m_matroska = std::move(port); }
    bool waitIdle(int ms = 30000);
    void setCloseWait(int ms) { m_closeWaitMs = ms; }

signals:
    void settingsChanged();
    void logChanged();
    void stageChanged();
    void styleRequested(const QString &style);

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
    QString currentVideo() const;
    void runMkvPrepare();
    void mkvNext(std::uint64_t run);
    void mkvPrepared(bool cancelled);
    void mkvApply();
    void mkvApplied(std::vector<application::MatroskaFontsSaved> saved, bool cancelled);
    void logMkvTab(std::size_t index);

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    std::unique_ptr<application::FontServicePort> m_service;
    std::unique_ptr<application::FontCollector> m_collector;
    int m_action = 0;
    QString m_directory;
    bool m_useSubsDirectory = false;
    QString m_displayRenderer = QStringLiteral("libass");
    std::vector<Segment> m_log;
    std::vector<Area> m_areas;
    int m_stage = Options;
    bool m_canSaveFolder = false;
    QString m_copyPath;
    QString m_pendingArchive;
    QString m_removeArchive;
    std::vector<application::CollectorDocument> m_documents;
    application::CollectorAction m_runAction = application::CollectorAction::Check;
    std::optional<application::CollectorReview> m_review;
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
    QElapsedTimer m_clock;
    qint64 m_elapsed = 0;
    int m_closeWaitMs = 30000;
    std::unique_ptr<application::MatroskaPort> m_matroska;
    bool m_fromMkv = false;
    bool m_fromMkvEnabled = false;
    bool m_runMkv = false;
    bool m_runAllTabs = false;
    bool m_runZip = false;
    std::vector<QString> m_mkvVideos;
    std::vector<application::MatroskaFontTab> m_mkvTabs;
    std::uint64_t m_mkvRun = 0;
};

} // namespace hikari::app
