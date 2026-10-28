#include "log_controller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMetaMethod>
#include <QThread>
#include <QTime>

#include <utility>

namespace hikari::ui {

namespace {
// More than the toasts can show at once; older ones stay in the Log only.
constexpr size_t kUnshownKept = 10;
}

class LogController::Model : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;

    std::vector<Entry> entries;

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(entries.size());
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= int(entries.size()))
            return {};
        const Entry &e = entries[size_t(index.row())];
        switch (role) {
        case LogEntries::IdRole: return e.id;
        case LogEntries::TimeRole: return e.time;
        case LogEntries::SeverityRole: return int(e.severity);
        case LogEntries::TitleRole: return e.title;
        case Qt::DisplayRole:
        case LogEntries::TextRole: return e.text;
        case LogEntries::DetailsRole: return e.details;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{LogEntries::IdRole, "entryId"},     {LogEntries::TimeRole, "time"},
                {LogEntries::SeverityRole, "severity"}, {LogEntries::TitleRole, "title"},
                {LogEntries::TextRole, "text"},      {LogEntries::DetailsRole, "details"}};
    }
    void append(Entry entry)
    {
        const int row = int(entries.size());
        beginInsertRows({}, row, row);
        entries.push_back(std::move(entry));
        endInsertRows();
    }
};

LogEntries::LogEntries(QObject *parent) : QSortFilterProxyModel(parent) {}

void LogEntries::setShown(int severity, bool on)
{
    if (severity < 0 || severity > 2 || m_show[severity] == on)
        return;
    m_show[severity] = on;
    invalidateRowsFilter();
    emit filterChanged();
}

void LogEntries::setFilterText(const QString &text)
{
    if (m_filter == text)
        return;
    m_filter = text;
    invalidateRowsFilter();
    emit filterChanged();
}

int LogEntries::rowOf(int id) const
{
    for (int row = 0; row < rowCount(); ++row)
        if (data(index(row, 0), IdRole).toInt() == id)
            return row;
    return -1;
}

bool LogEntries::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex i = sourceModel()->index(sourceRow, 0, sourceParent);
    const int severity = sourceModel()->data(i, SeverityRole).toInt();
    if (severity >= 0 && severity <= 2 && !m_show[severity])
        return false;
    const QString needle = m_filter.trimmed();
    if (needle.isEmpty())
        return true;
    for (int role : {TitleRole, TextRole, DetailsRole})
        if (sourceModel()->data(i, role).toString().contains(needle, Qt::CaseInsensitive))
            return true;
    return false;
}

LogController::LogController(QObject *parent)
    : QObject(parent), m_model(new Model(this)), m_entries(new LogEntries(this)),
      m_clock([] { return QTime::currentTime().toString(QStringLiteral("HH:mm:ss")); })
{
    m_entries->setSourceModel(m_model);
}

LogController::~LogController() = default;

QString LogController::lastMessage() const
{
    return m_model->entries.empty() ? QString() : m_model->entries.back().text;
}

QString LogController::lineOf(const Entry &entry)
{
    QString line = entry.time + QLatin1Char(' ');
    if (!entry.title.isEmpty())
        line += entry.title + QStringLiteral(": ");
    line += entry.text + QLatin1Char('\n');
    if (!entry.details.isEmpty())
        for (const QString &detail : entry.details.split(QLatin1Char('\n')))
            line += QStringLiteral("    ") + detail + QLatin1Char('\n');
    return line;
}

QString LogController::history() const
{
    QString text;
    for (const Entry &entry : m_model->entries)
        text += lineOf(entry);
    return text;
}

int LogController::add(Severity severity, const QString &title, const QString &text, const QString &details)
{
    const int id = int(m_model->entries.size()) + 1;
    const QString time = m_clock();
    m_model->append({id, time, severity, title, text, details});
    ++m_counts[severity];
    emit changed();
    emit added(time, severity, title, text, details);
    return id;
}

void LogController::log(const QString &text, Severity severity, const QString &title)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [=, this] { log(text, severity, title); }, Qt::QueuedConnection);
        return;
    }
    add(severity, title, text, {});
}

void LogController::notice(Severity severity, const QString &title, const QString &text, const QString &details)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [=, this] { notice(severity, title, text, details); },
                                  Qt::QueuedConnection);
        return;
    }
    const int id = add(severity, title, text, details);
    if (!isSignalConnected(QMetaMethod::fromSignal(&LogController::noticed))) {
        m_unshown.push_back(id);
        if (m_unshown.size() > kUnshownKept)
            m_unshown.erase(m_unshown.begin());
        return;
    }
    emit noticed(id, severity, title, text, details);
}

void LogController::replayNotices()
{
    for (const int id : std::exchange(m_unshown, {})) {
        const Entry &entry = m_model->entries[size_t(id - 1)];
        emit noticed(id, entry.severity, entry.title, entry.text, entry.details);
    }
}

void LogController::reveal(int id)
{
    if (id > 0 && id <= int(m_model->entries.size())) {
        const Entry &entry = m_model->entries[size_t(id - 1)];
        m_entries->setShown(entry.severity, true);
        if (m_entries->rowOf(id) < 0)
            m_entries->setFilterText({});
    }
    if (m_marked != id) {
        m_marked = id;
        emit markedChanged();
    }
    emit revealRequested();
}

QString LogController::shownText() const
{
    QString text;
    for (int row = 0; row < m_entries->rowCount(); ++row) {
        const int id = m_entries->data(m_entries->index(row, 0), LogEntries::IdRole).toInt();
        text += lineOf(m_model->entries[size_t(id - 1)]);
    }
    return text;
}

void LogController::copyShown() const
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(shownText());
}

void LogController::copyEntry(int id) const
{
    auto *clipboard = QGuiApplication::clipboard();
    if (clipboard && id > 0 && id <= int(m_model->entries.size()))
        clipboard->setText(lineOf(m_model->entries[size_t(id - 1)]));
}

} // namespace hikari::ui
