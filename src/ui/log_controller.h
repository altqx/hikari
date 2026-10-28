#pragma once

// The Log's presenter (P5; legacy LogHandler/LogWindow at 20d647c4). NT1
// (#302): every message is kept as an entry with its local time
// ("HH:MM:SS"), a severity, the command that gave it (its title), its text
// and any details. The Log panel shows them through `entries`, filtered by
// severity and by text; nothing opens it by itself any more.
// * log(text): an entry that only goes into the Log (legacy HikariLogSilent).
// * notice(severity, title, text, details): a notice, which also asks for a
//   toast (`noticed`, which Main's ToastStack shows); legacy HikariLog,
//   which popped the Log window up.
// * reveal(id): a toast's Details; marks that entry and asks for the panel.
// Messages may come from any thread; they are published on this object's
// thread.

#include <QAbstractListModel>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <vector>

namespace hikari::ui {

class LogController;

// The entries as the panel shows them: the severities switched on and,
// with a filter text, those whose title, text or details contain it.
class LogEntries : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The Log's entries (LogController.entries)")
    Q_PROPERTY(bool showInfo READ showInfo WRITE setShowInfo NOTIFY filterChanged)
    Q_PROPERTY(bool showWarnings READ showWarnings WRITE setShowWarnings NOTIFY filterChanged)
    Q_PROPERTY(bool showErrors READ showErrors WRITE setShowErrors NOTIFY filterChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
public:
    enum Role { IdRole = Qt::UserRole + 1, TimeRole, SeverityRole, TitleRole, TextRole, DetailsRole };

    explicit LogEntries(QObject *parent = nullptr);

    bool showInfo() const { return m_show[0]; }
    bool showWarnings() const { return m_show[1]; }
    bool showErrors() const { return m_show[2]; }
    QString filterText() const { return m_filter; }
    void setShowInfo(bool on) { setShown(0, on); }
    void setShowWarnings(bool on) { setShown(1, on); }
    void setShowErrors(bool on) { setShown(2, on); }
    void setShown(int severity, bool on);
    void setFilterText(const QString &text);
    // The shown row of entry `id`, or -1.
    Q_INVOKABLE int rowOf(int id) const;

signals:
    void filterChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool m_show[3] = {true, true, true};
    QString m_filter;
};

class LogController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // The last entry's text, and the whole Log as text (one "HH:MM:SS"
    // line per entry, its details indented below), as Copy writes it.
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY changed)
    Q_PROPERTY(QString history READ history NOTIFY changed)
    Q_PROPERTY(LogEntries *entries READ entries CONSTANT)
    Q_PROPERTY(int infoCount READ infoCount NOTIFY changed)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY changed)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY changed)
    // The entry a toast's Details asked for (0: none).
    Q_PROPERTY(int marked READ marked NOTIFY markedChanged)
public:
    enum Severity { Info, Warning, Error };
    Q_ENUM(Severity)

    explicit LogController(QObject *parent = nullptr);
    ~LogController() override;

    QString lastMessage() const;
    QString history() const;
    LogEntries *entries() const { return m_entries; }
    int infoCount() const { return m_counts[Info]; }
    int warningCount() const { return m_counts[Warning]; }
    int errorCount() const { return m_counts[Error]; }
    int marked() const { return m_marked; }

    // HikariLogSilent: into the Log only (an error whose toast another
    // message stands for: a severity and a title).
    Q_INVOKABLE void log(const QString &text, hikari::ui::LogController::Severity severity = Info,
                         const QString &title = {});
    // HikariLog: into the Log, and a toast (`noticed`).
    Q_INVOKABLE void notice(hikari::ui::LogController::Severity severity, const QString &title, const QString &text,
                            const QString &details = {});
    // The notices given before anything took `noticed` (those of the start,
    // before Main's toasts are made), given again; the toasts call it once
    // connected.
    Q_INVOKABLE void replayNotices();
    // A toast's Details, or "N more notices" (id 0): marks the entry, shows
    // it whatever the filters, and asks for the panel (`revealRequested`).
    Q_INVOKABLE void reveal(int id);
    // Copy: the entries shown, as text.
    Q_INVOKABLE QString shownText() const;
    Q_INVOKABLE void copyShown() const;
    // Ctrl+C on an entry of the panel.
    Q_INVOKABLE void copyEntry(int id) const;
    // Tests: the clock that stamps messages.
    void setClock(std::function<QString()> clock) { m_clock = std::move(clock); }

signals:
    void changed();
    void markedChanged();
    void noticed(int id, hikari::ui::LogController::Severity severity, const QString &title, const QString &text,
                 const QString &details);
    // Every entry as it is added (CR1's Log tail for reports).
    void added(const QString &time, hikari::ui::LogController::Severity severity, const QString &title,
               const QString &text, const QString &details);
    void revealRequested();

private:
    struct Entry {
        int id;
        QString time;
        Severity severity;
        QString title;
        QString text;
        QString details;
    };
    class Model;
    int add(Severity severity, const QString &title, const QString &text, const QString &details);
    static QString lineOf(const Entry &entry);

    Model *m_model;
    LogEntries *m_entries;
    int m_counts[3] = {0, 0, 0};
    int m_marked = 0;
    // Entries noticed with nothing connected (the last few).
    std::vector<int> m_unshown;
    std::function<QString()> m_clock;
};

} // namespace hikari::ui
