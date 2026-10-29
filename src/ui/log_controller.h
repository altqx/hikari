#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <vector>

namespace hikari::ui {

class LogController;

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
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY changed)
    Q_PROPERTY(QString history READ history NOTIFY changed)
    Q_PROPERTY(LogEntries *entries READ entries CONSTANT)
    Q_PROPERTY(int infoCount READ infoCount NOTIFY changed)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY changed)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY changed)
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

    Q_INVOKABLE void log(const QString &text, hikari::ui::LogController::Severity severity = Info,
                         const QString &title = {});
    Q_INVOKABLE void notice(hikari::ui::LogController::Severity severity, const QString &title, const QString &text,
                            const QString &details = {});
    Q_INVOKABLE void replayNotices();
    // MUST KILL: reveal(0) means the "N more notices" toast, not an entry
    Q_INVOKABLE void reveal(int id);
    Q_INVOKABLE QString shownText() const;
    Q_INVOKABLE void copyShown() const;
    Q_INVOKABLE void copyEntry(int id) const;
    void setClock(std::function<QString()> clock) { m_clock = std::move(clock); }

signals:
    void changed();
    void markedChanged();
    void noticed(int id, hikari::ui::LogController::Severity severity, const QString &title, const QString &text,
                 const QString &details);
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
    std::vector<int> m_unshown;
    std::function<QString()> m_clock;
};

}
