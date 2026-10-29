#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <deque>
#include <functional>
#include <vector>

namespace hikari::ui {

class AutomationRuns : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the automation shell (automation.runs)")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    static constexpr int kKept = 20;
    static constexpr int kMaxLines = 10'000;
    static constexpr qsizetype kMaxChars = 4'000'000;
    static constexpr int kMaxNoteLines = 200;
    static constexpr qsizetype kMaxText = 4'000;
    // MUST KILL: line levels mix Aegisub's debug.out levels (0 fatal … 5 trace) with these negative sentinels
    static constexpr int kNoLevel = -1;
    static constexpr int kHostError = -2;
    static constexpr int kHostInfo = -3;
    enum Role {
        IdRole = Qt::UserRole + 1,
        MacroRole,
        ScriptRole,
        DocumentRole,
        StartedRole,
        StateRole,
        TaskRole,
        ProgressRole,
        DurationRole,
        LineCountRole,
        MessageRole,
    };
    struct Line {
        int level = kNoLevel;
        qint64 elapsed = 0;
        QString text;
    };
    struct Run {
        int id = 0;
        QString macro, script, document;
        QDateTime started;
        qint64 startedAt = 0;
        // MUST KILL: duration -1 doubles as the run's still-running flag
        qint64 duration = -1;
        QString state = QStringLiteral("running");
        QString task, message;
        double progress = -1;
        std::vector<Line> lines;
        bool open = false;
        qsizetype chars = 0;
        bool full = false;
    };

    explicit AutomationRuns(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_runs.size()); }

    int begin(const QString &macro, const QString &script, const QString &document);
    void append(int run, const QString &text, int level = kNoLevel);
    void note(int run, const QString &text, int kind);
    void setTask(int run, const QString &task);
    void setProgress(int run, double percent);
    void finish(int run, const QString &state, const QString &message);

    const Run *run(int id) const;
    QString text(int id, bool times, const std::function<bool(const Line &)> &accept = {}, bool header = false) const;
    static QString levelName(int level);
    static QString elapsedText(qint64 ms);

    Q_INVOKABLE int rowOf(int id) const;
    Q_INVOKABLE int idAt(int row) const;
    Q_INVOKABLE QString documentOf(int id) const;
    Q_INVOKABLE void clear();

    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    void setWallClock(std::function<QDateTime()> clock) { m_wallClock = std::move(clock); }

signals:
    void countChanged();
    void linesAdded(int run, int first, int last, int changed);
    void removed(int run);

private:
    Run *find(int id);
    void changedRow(int id, const QList<int> &roles);
    qint64 now() const;

    std::deque<Run> m_runs;
    int m_nextId = 1;
    std::function<qint64()> m_clock;
    std::function<QDateTime()> m_wallClock;
};

class AutomationRunLines : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(hikari::ui::AutomationRuns *runs READ runs WRITE setRuns NOTIFY runsChanged)
    Q_PROPERTY(int run READ run WRITE setRun NOTIFY runChanged)
    Q_PROPERTY(int levelLimit READ levelLimit WRITE setLevelLimit NOTIFY filterChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(bool times READ times WRITE setTimes NOTIFY timesChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
public:
    enum Role { LevelRole = Qt::UserRole + 1, LevelNameRole, TextRole, TimeRole };

    explicit AutomationRunLines(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    AutomationRuns *runs() const { return m_runs; }
    void setRuns(AutomationRuns *runs);
    int run() const { return m_run; }
    void setRun(int run);
    int levelLimit() const { return m_levelLimit; }
    void setLevelLimit(int level);
    QString filterText() const { return m_filter; }
    void setFilterText(const QString &text);
    bool times() const { return m_times; }
    void setTimes(bool on);
    int count() const { return static_cast<int>(m_shown.size()); }
    int total() const;

    Q_INVOKABLE int countAt(int level) const;
    Q_INVOKABLE QString shownText() const;
    Q_INVOKABLE void copyShown() const;
    Q_INVOKABLE QString saveShown(const QUrl &file) const;
    Q_INVOKABLE QString suggestedName() const;

signals:
    void runsChanged();
    void runChanged();
    void filterChanged();
    void timesChanged();
    void countChanged();

private:
    bool accepts(const AutomationRuns::Line &line) const;
    void rebuild();
    void onAdded(int run, int first, int last, int changed);
    const AutomationRuns::Run *current() const;

    QPointer<AutomationRuns> m_runs;
    int m_run = 0;
    int m_levelLimit = 5;
    QString m_filter;
    bool m_times = false;
    std::vector<int> m_shown;
};

}
