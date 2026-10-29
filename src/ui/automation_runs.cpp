#include "automation_runs.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QSaveFile>

#include <algorithm>

namespace hikari::ui {

namespace {

QElapsedTimer &monotonic()
{
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer;
}

}

AutomationRuns::AutomationRuns(QObject *parent) : QAbstractListModel(parent) {}

qint64 AutomationRuns::now() const
{
    return m_clock ? m_clock() : monotonic().elapsed();
}

int AutomationRuns::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant AutomationRuns::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= count())
        return {};
    const Run &r = m_runs[static_cast<std::size_t>(index.row())];
    switch (role) {
    case IdRole: return r.id;
    case MacroRole: return r.macro;
    case ScriptRole: return r.script;
    case DocumentRole: return r.document;
    case StartedRole: return r.started.toString(QStringLiteral("HH:mm"));
    case StateRole: return r.state;
    case TaskRole: return r.task;
    case ProgressRole: return r.progress;
    case DurationRole: return r.duration >= 0 ? r.duration : now() - r.startedAt;
    case LineCountRole: return static_cast<int>(r.lines.size());
    case MessageRole: return r.message;
    default: return {};
    }
}

QHash<int, QByteArray> AutomationRuns::roleNames() const
{
    return {{IdRole, "runId"},       {MacroRole, "macro"},       {ScriptRole, "script"},
            {DocumentRole, "document"}, {StartedRole, "started"}, {StateRole, "runState"},
            {TaskRole, "task"},       {ProgressRole, "progress"}, {DurationRole, "duration"},
            {LineCountRole, "lineCount"}, {MessageRole, "message"}};
}

AutomationRuns::Run *AutomationRuns::find(int id)
{
    auto it = std::ranges::find(m_runs, id, &Run::id);
    return it == m_runs.end() ? nullptr : &*it;
}

const AutomationRuns::Run *AutomationRuns::run(int id) const
{
    auto it = std::ranges::find(m_runs, id, &Run::id);
    return it == m_runs.end() ? nullptr : &*it;
}

int AutomationRuns::rowOf(int id) const
{
    auto it = std::ranges::find(m_runs, id, &Run::id);
    return it == m_runs.end() ? -1 : static_cast<int>(it - m_runs.begin());
}

int AutomationRuns::idAt(int row) const
{
    return row >= 0 && row < count() ? m_runs[static_cast<std::size_t>(row)].id : 0;
}

QString AutomationRuns::documentOf(int id) const
{
    const Run *r = run(id);
    return r ? r->document : QString();
}

void AutomationRuns::changedRow(int id, const QList<int> &roles)
{
    const int row = rowOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row), roles);
}

int AutomationRuns::begin(const QString &macro, const QString &script, const QString &document)
{
    while (count() >= kKept) {
        auto it = std::ranges::find_if(m_runs.rbegin(), m_runs.rend(), [](const Run &r) { return r.duration >= 0; });
        if (it == m_runs.rend())
            break;
        const int row = static_cast<int>(m_runs.rend() - it) - 1;
        const int id = it->id;
        beginRemoveRows({}, row, row);
        m_runs.erase(m_runs.begin() + row);
        endRemoveRows();
        emit removed(id);
    }
    Run r;
    r.id = m_nextId++;
    r.macro = macro.left(kMaxText);
    r.script = script.left(kMaxText);
    r.document = document.left(kMaxText);
    r.started = m_wallClock ? m_wallClock() : QDateTime::currentDateTime();
    r.startedAt = now();
    beginInsertRows({}, 0, 0);
    m_runs.push_front(std::move(r));
    endInsertRows();
    emit countChanged();
    return m_runs.front().id;
}

void AutomationRuns::append(int id, const QString &text, int level)
{
    Run *r = find(id);
    if (!r || text.isEmpty() || r->full)
        return;
    level = level == kNoLevel ? kNoLevel : std::max(level, 0);
    const qint64 at = now() - r->startedAt;
    int changed = -1;
    const int first = static_cast<int>(r->lines.size());
    qsizetype from = 0;
    while (from < text.size()) {
        if (r->lines.size() >= static_cast<std::size_t>(kMaxLines) || r->chars >= kMaxChars) {
            r->full = true;
            break;
        }
        const qsizetype newline = text.indexOf(QLatin1Char('\n'), from);
        const qsizetype end = newline < 0 ? text.size() : newline;
        const qsizetype length = std::min<qsizetype>(end - from, kMaxChars - r->chars);
        if (length < end - from)
            r->full = true;
        const QString piece = text.mid(from, length);
        r->chars += length;
        if (from == 0 && r->open && !r->lines.empty()) {
            r->lines.back().text += piece;
            changed = static_cast<int>(r->lines.size()) - 1;
        } else {
            r->lines.push_back({level, at, piece});
        }
        r->open = newline < 0;
        if (newline < 0 || r->full)
            break;
        from = newline + 1;
        if (from == text.size())
            r->open = false;
    }
    if (r->full) {
        r->lines.push_back({kHostInfo, at,
                            QCoreApplication::translate("HikariSub.Automation",
                                                        "The script wrote more than this log keeps; the rest is not kept.")});
        r->open = false;
    }
    const int last = static_cast<int>(r->lines.size()) - 1;
    if (last >= first || changed >= 0)
        emit linesAdded(id, first, last, changed);
    changedRow(id, {LineCountRole});
}

void AutomationRuns::note(int id, const QString &text, int kind)
{
    Run *r = find(id);
    if (!r || text.isEmpty())
        return;
    const qint64 at = now() - r->startedAt;
    const int first = static_cast<int>(r->lines.size());
    QString body = text;
    while (body.endsWith(QLatin1Char('\n')))
        body.chop(1);
    bool cut = body.size() > kMaxChars;
    body.truncate(kMaxChars);
    qsizetype from = 0;
    for (int i = 0;; ++i) {
        if (i == kMaxNoteLines) {
            cut = true;
            break;
        }
        const qsizetype newline = body.indexOf(QLatin1Char('\n'), from);
        r->lines.push_back({i == 0 ? kind : kNoLevel, at, body.mid(from, newline < 0 ? -1 : newline - from)});
        if (newline < 0)
            break;
        from = newline + 1;
    }
    if (cut)
        r->lines.push_back({kHostInfo, at,
                            QCoreApplication::translate("HikariSub.Automation", "The rest of this message is not kept.")});
    r->open = false;
    emit linesAdded(id, first, static_cast<int>(r->lines.size()) - 1, -1);
    changedRow(id, {LineCountRole});
}

void AutomationRuns::setTask(int id, const QString &task)
{
    if (Run *r = find(id); r && r->task != task.left(kMaxText)) {
        r->task = task.left(kMaxText);
        changedRow(id, {TaskRole});
    }
}

void AutomationRuns::setProgress(int id, double percent)
{
    if (Run *r = find(id); r && r->progress != percent) {
        r->progress = percent;
        changedRow(id, {ProgressRole, DurationRole});
    }
}

void AutomationRuns::finish(int id, const QString &state, const QString &message)
{
    Run *r = find(id);
    if (!r)
        return;
    r->state = state;
    r->message = message.left(kMaxText);
    r->duration = now() - r->startedAt;
    r->open = false;
    changedRow(id, {StateRole, MessageRole, DurationRole});
}

void AutomationRuns::clear()
{
    for (int row = count() - 1; row >= 0; --row) {
        if (m_runs[static_cast<std::size_t>(row)].duration < 0)
            continue;
        const int id = m_runs[static_cast<std::size_t>(row)].id;
        beginRemoveRows({}, row, row);
        m_runs.erase(m_runs.begin() + row);
        endRemoveRows();
        emit removed(id);
    }
    emit countChanged();
}

QString AutomationRuns::levelName(int level)
{
    switch (level) {
    case 0: return QCoreApplication::translate("HikariSub.Automation", "fatal");
    case 1: return QCoreApplication::translate("HikariSub.Automation", "error");
    case 2: return QCoreApplication::translate("HikariSub.Automation", "warning");
    case 3: return QCoreApplication::translate("HikariSub.Automation", "hint");
    case 4: return QCoreApplication::translate("HikariSub.Automation", "debug");
    case 5: return QCoreApplication::translate("HikariSub.Automation", "trace");
    default: return level > 5 ? QString::number(level) : QString();
    }
}

QString AutomationRuns::elapsedText(qint64 ms)
{
    ms = std::max<qint64>(ms, 0);
    return QStringLiteral("%1:%2.%3")
        .arg(ms / 60000)
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString AutomationRuns::text(int id, bool times, const std::function<bool(const Line &)> &accept, bool header) const
{
    const Run *r = run(id);
    if (!r)
        return {};
    QString out;
    if (header) {
        out += r->macro + QStringLiteral(" (") + r->script + QStringLiteral(")\n");
        if (!r->document.isEmpty())
            out += r->document + QLatin1Char('\n');
        out += r->started.toString(Qt::ISODate) + QStringLiteral(", ") + r->state + QLatin1Char('\n');
        if (!r->message.isEmpty())
            out += r->message + QLatin1Char('\n');
        out += QLatin1Char('\n');
    }
    for (const Line &line : r->lines) {
        if (accept && !accept(line))
            continue;
        if (times)
            out += elapsedText(line.elapsed) + QLatin1Char(' ');
        if (line.level >= 0)
            out += QLatin1Char('[') + levelName(line.level) + QStringLiteral("] ");
        out += line.text + QLatin1Char('\n');
    }
    return out;
}

AutomationRunLines::AutomationRunLines(QObject *parent) : QAbstractListModel(parent) {}

int AutomationRunLines::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

const AutomationRuns::Run *AutomationRunLines::current() const
{
    return m_runs ? m_runs->run(m_run) : nullptr;
}

QVariant AutomationRunLines::data(const QModelIndex &index, int role) const
{
    const auto *r = current();
    if (!r || !index.isValid() || index.row() >= count())
        return {};
    const auto &line = r->lines[static_cast<std::size_t>(m_shown[static_cast<std::size_t>(index.row())])];
    switch (role) {
    case LevelRole: return line.level;
    case LevelNameRole: return AutomationRuns::levelName(line.level);
    case TextRole: return line.text;
    case TimeRole: return AutomationRuns::elapsedText(line.elapsed);
    default: return {};
    }
}

QHash<int, QByteArray> AutomationRunLines::roleNames() const
{
    return {{LevelRole, "level"}, {LevelNameRole, "levelName"}, {TextRole, "text"}, {TimeRole, "time"}};
}

void AutomationRunLines::setRuns(AutomationRuns *runs)
{
    if (m_runs == runs)
        return;
    if (m_runs)
        disconnect(m_runs, nullptr, this, nullptr);
    m_runs = runs;
    if (m_runs) {
        connect(m_runs, &AutomationRuns::linesAdded, this, &AutomationRunLines::onAdded);
        connect(m_runs, &AutomationRuns::removed, this, [this](int id) {
            if (id == m_run)
                rebuild();
        });
    }
    rebuild();
    emit runsChanged();
}

void AutomationRunLines::setRun(int run)
{
    if (m_run == run)
        return;
    m_run = run;
    rebuild();
    emit runChanged();
}

void AutomationRunLines::setLevelLimit(int level)
{
    level = std::clamp(level, 0, 5);
    if (m_levelLimit == level)
        return;
    m_levelLimit = level;
    rebuild();
    emit filterChanged();
}

void AutomationRunLines::setFilterText(const QString &text)
{
    if (m_filter == text)
        return;
    m_filter = text;
    rebuild();
    emit filterChanged();
}

void AutomationRunLines::setTimes(bool on)
{
    if (m_times == on)
        return;
    m_times = on;
    emit timesChanged();
}

int AutomationRunLines::total() const
{
    const auto *r = current();
    return r ? static_cast<int>(r->lines.size()) : 0;
}

bool AutomationRunLines::accepts(const AutomationRuns::Line &line) const
{
    if (line.level > m_levelLimit)
        return false;
    const QString needle = m_filter.trimmed();
    return needle.isEmpty() || line.text.contains(needle, Qt::CaseInsensitive);
}

void AutomationRunLines::rebuild()
{
    beginResetModel();
    m_shown.clear();
    if (const auto *r = current()) {
        for (std::size_t i = 0; i < r->lines.size(); ++i)
            if (accepts(r->lines[i]))
                m_shown.push_back(static_cast<int>(i));
    }
    endResetModel();
    emit countChanged();
}

void AutomationRunLines::onAdded(int run, int first, int last, int changed)
{
    const auto *r = current();
    if (run != m_run || !r)
        return;
    if (changed >= 0) {
        const bool takes = accepts(r->lines[static_cast<std::size_t>(changed)]);
        const auto it = std::ranges::find(m_shown, changed);
        if (it != m_shown.end()) {
            const int row = static_cast<int>(it - m_shown.begin());
            if (takes) {
                emit dataChanged(index(row), index(row), {TextRole});
            } else {
                beginRemoveRows({}, row, row);
                m_shown.erase(it);
                endRemoveRows();
            }
        } else if (takes) {
            const int row = static_cast<int>(m_shown.size());
            beginInsertRows({}, row, row);
            m_shown.push_back(changed);
            endInsertRows();
        }
    }
    std::vector<int> added;
    for (int i = first; i <= last; ++i)
        if (i != changed && accepts(r->lines[static_cast<std::size_t>(i)]))
            added.push_back(i);
    if (!added.empty()) {
        const int row = static_cast<int>(m_shown.size());
        beginInsertRows({}, row, row + static_cast<int>(added.size()) - 1);
        m_shown.insert(m_shown.end(), added.begin(), added.end());
        endInsertRows();
    }
    emit countChanged();
}

int AutomationRunLines::countAt(int level) const
{
    const auto *r = current();
    if (!r)
        return 0;
    return static_cast<int>(std::ranges::count(r->lines, level, &AutomationRuns::Line::level));
}

QString AutomationRunLines::shownText() const
{
    if (!m_runs)
        return {};
    return m_runs->text(m_run, m_times, [this](const AutomationRuns::Line &l) { return accepts(l); });
}

void AutomationRunLines::copyShown() const
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(shownText());
}

QString AutomationRunLines::saveShown(const QUrl &file) const
{
    if (!m_runs || !current())
        return QCoreApplication::translate("HikariSub.Automation", "The run is no longer kept.");
    QSaveFile out(file.isLocalFile() ? file.toLocalFile() : file.toString());
    const QByteArray bytes =
        m_runs->text(m_run, m_times, [this](const AutomationRuns::Line &l) { return accepts(l); }, true).toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        return out.errorString();
    return {};
}

QString AutomationRunLines::suggestedName() const
{
    const auto *r = current();
    if (!r)
        return {};
    QString name = r->macro;
    for (QChar &c : name)
        if (QStringLiteral("\\/:*?\"<>|").contains(c) || c.unicode() < 32)
            c = QLatin1Char('_');
    return name + QLatin1Char(' ') + r->started.toString(QStringLiteral("yyyy-MM-dd HH-mm")) + QStringLiteral(".log");
}

}
