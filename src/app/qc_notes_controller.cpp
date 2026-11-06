#include "hikari/app/qc_notes_controller.h"

#include "hikari/core/text_projection.h"
#include "settings_store.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <limits>

namespace hikari::app {

namespace {

constexpr const char *kHideDone = "qcNotes.hideDone";
constexpr const char *kLastSection = "qcNotes.lastSection";

QString qt(std::u8string_view text)
{
    return QString::fromStdU16String(core::toUtf16(text));
}

std::u8string u8(const QString &text)
{
    return core::toUtf8(text.toStdU16String());
}

QString shownText(const std::u8string &text)
{
    return qt(text).replace(QStringLiteral("\\N"), QStringLiteral("\n"));
}

application::QcNote noteOf(int ms, const QString &section, const QString &text, bool done)
{
    return application::normalisedQcNote({std::max(ms, 0), u8(section), done, u8(text)});
}

}

QcNotesController::QcNotesController(ui::SettingsStore &settings, Hooks hooks, QObject *parent)
    : QObject(parent), m_settings(settings), m_hooks(std::move(hooks))
{
    if (auto *clipboard = QGuiApplication::clipboard())
        connect(clipboard, &QClipboard::dataChanged, this, &QcNotesController::clipboardChanged);
}

application::EditSession *QcNotesController::target() const
{
    return m_hooks.target ? m_hooks.target() : nullptr;
}

bool QcNotesController::shown(std::size_t index) const
{
    const auto &note = m_notes[index];
    return (m_section.isEmpty() || qt(note.section) == m_section) && !(note.done && hideDone());
}

QVariantList QcNotesController::notes() const
{
    QVariantList out;
    for (std::size_t i = 0; i < m_notes.size(); ++i) {
        if (!shown(i))
            continue;
        const auto &note = m_notes[i];
        out.push_back(QVariantMap{{QStringLiteral("index"), int(i)},
                                  {QStringLiteral("time"), qlonglong(note.ms)},
                                  {QStringLiteral("timeText"), timeText(int(note.ms))},
                                  {QStringLiteral("section"), qt(note.section)},
                                  {QStringLiteral("done"), note.done},
                                  {QStringLiteral("text"), shownText(note.text)},
                                  {QStringLiteral("row"), m_rows[i]}});
    }
    return out;
}

QVariantList QcNotesController::sections() const
{
    QVariantList out;
    QStringList names;
    std::vector<std::pair<int, int>> counts;
    for (const auto &note : m_notes) {
        const QString name = qt(note.section);
        auto at = names.indexOf(name);
        if (at < 0) {
            at = int(names.size());
            names.push_back(name);
            counts.emplace_back(0, 0);
        }
        ++counts[std::size_t(at)].first;
        if (!note.done)
            ++counts[std::size_t(at)].second;
    }
    for (qsizetype i = 0; i < names.size(); ++i)
        out.push_back(QVariantMap{{QStringLiteral("name"), names[i]},
                                  {QStringLiteral("count"), counts[std::size_t(i)].first},
                                  {QStringLiteral("open"), counts[std::size_t(i)].second}});
    return out;
}

void QcNotesController::setSection(const QString &section)
{
    if (section == m_section)
        return;
    m_section = section;
    publishMarks();
    emit changed();
}

bool QcNotesController::hideDone() const
{
    return m_settings.boolean(kHideDone);
}

void QcNotesController::setHideDone(bool hide)
{
    if (hide == hideDone())
        return;
    m_settings.set(kHideDone, hide);
    publishMarks();
    emit changed();
}

int QcNotesController::current() const
{
    const auto index = indexOfCurrent();
    return index && shown(*index) ? int(*index) : -1;
}

bool QcNotesController::hasDocument() const
{
    return target() != nullptr;
}

bool QcNotesController::editable() const
{
    return target() && m_hooks.editable && m_hooks.editable();
}

QString QcNotesController::lastSection() const
{
    const QString section = m_settings.text(kLastSection);
    return section.isEmpty() ? qt(application::kQcGeneralSection) : section;
}

bool QcNotesController::clipboardHasNotes() const
{
    return !application::parseQcNoteText(u8(clipboardText())).empty();
}

int QcNotesController::addTime() const
{
    if (const int video = videoTime(); video >= 0)
        return video;
    return std::max(lineStart(), 0);
}

int QcNotesController::lineStart() const
{
    const auto *session = target();
    const auto active = session ? session->selection().active : std::nullopt;
    if (!active)
        return -1;
    for (const auto *line : session->document().lines())
        if (line->id == *active)
            return int(line->start.value.microseconds() / 1000);
    return -1;
}

int QcNotesController::videoTime() const
{
    const auto time = m_hooks.videoTime ? m_hooks.videoTime() : std::nullopt;
    return time ? *time : -1;
}

bool QcNotesController::add(int ms, const QString &section, const QString &text)
{
    const auto note = noteOf(ms, section, text, false);
    if (note.text.empty())
        return false;
    auto notes = m_notes;
    notes.push_back(note);
    if (!write(std::move(notes), "Add QC note", note))
        return false;
    m_settings.set(kLastSection, qt(note.section));
    emit changed();
    return true;
}

bool QcNotesController::edit(int index, int ms, const QString &section, const QString &text, bool done)
{
    if (index < 0 || std::size_t(index) >= m_notes.size())
        return false;
    const auto note = noteOf(ms, section, text, done);
    if (note.text.empty())
        return false;
    auto notes = m_notes;
    notes[std::size_t(index)] = note;
    if (!write(std::move(notes), "Edit QC note", note))
        return false;
    m_settings.set(kLastSection, qt(note.section));
    emit changed();
    return true;
}

bool QcNotesController::setDone(int index, bool done)
{
    if (index < 0 || std::size_t(index) >= m_notes.size())
        return false;
    auto notes = m_notes;
    notes[std::size_t(index)].done = done;
    return write(notes, "Mark QC note done", notes[std::size_t(index)]);
}

bool QcNotesController::remove(int index)
{
    if (index < 0 || std::size_t(index) >= m_notes.size())
        return false;
    auto notes = m_notes;
    notes.erase(notes.begin() + index);
    return write(std::move(notes), "Delete QC note", std::nullopt);
}

bool QcNotesController::markAllNotDone()
{
    auto notes = m_notes;
    for (auto &note : notes)
        note.done = false;
    return write(std::move(notes), "Mark QC notes not done", std::nullopt);
}

bool QcNotesController::removeAll()
{
    return write({}, "Delete QC notes", std::nullopt);
}

QVariantMap QcNotesController::readFile(const QUrl &url) const
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly))
        return {{QStringLiteral("error"), file.errorString()}};
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return {{QStringLiteral("text"), text}};
}

QString QcNotesController::clipboardText() const
{
    const auto *clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text() : QString();
}

QVariantMap QcNotesController::preview(const QString &text) const
{
    const auto incoming = application::parseQcNoteText(u8(text));
    std::vector<std::u8string> names;
    for (const auto &note : incoming)
        if (std::ranges::find(names, note.section) == names.end())
            names.push_back(note.section);
    return {{QStringLiteral("count"), int(incoming.size())},
            {QStringLiteral("sections"), int(names.size())},
            {QStringLiteral("existing"), int(m_notes.size())},
            {QStringLiteral("duplicates"), int(application::importQcNotes(m_notes, incoming, false).skipped)}};
}

QVariantMap QcNotesController::importNotes(const QString &text, bool replace)
{
    const auto incoming = application::parseQcNoteText(u8(text));
    if (incoming.empty())
        return {};
    auto result = application::importQcNotes(m_notes, incoming, replace);
    if (result.notes != m_notes && !write(std::move(result.notes), "Import QC notes", std::nullopt))
        return {};
    return {{QStringLiteral("added"), int(result.added)}, {QStringLiteral("skipped"), int(result.skipped)}};
}

QString QcNotesController::exportText(bool includeDone, const QString &section) const
{
    std::vector<application::QcNote> chosen;
    for (const auto &note : m_notes)
        if ((includeDone || !note.done) && (section.isEmpty() || qt(note.section) == section))
            chosen.push_back(note);
    return qt(application::formatQcNoteText(chosen));
}

bool QcNotesController::writeFile(const QUrl &url, const QString &text) const
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = text.toUtf8();
    return file.write(bytes) == bytes.size();
}

bool QcNotesController::goTo(int index)
{
    if (index < 0 || std::size_t(index) >= m_notes.size())
        return false;
    const auto i = std::size_t(index);
    m_current = m_notes[i];
    int seek = int(m_notes[i].ms);
    if (const auto line = m_lines[i]) {
        if (m_hooks.selectLine)
            m_hooks.selectLine(*line);
        const auto [start, end] = m_lineSpansMs[i];
        if (seek < start || seek >= end)
            seek = start;
    }
    if (m_hooks.seek)
        m_hooks.seek(seek);
    publishMarks();
    emit changed();
    return true;
}

bool QcNotesController::next()
{
    return step(true);
}

bool QcNotesController::previous()
{
    return step(false);
}

bool QcNotesController::step(bool forward)
{
    const auto from = indexOfCurrent();
    std::int64_t time = 0;
    if (!from) {
        time = videoTime();
        if (time < 0)
            time = std::max(lineStart(), 0);
    }
    const auto after = [&](std::size_t i) {
        if (from)
            return forward ? i > *from : i < *from;
        return forward ? m_notes[i].ms >= time : m_notes[i].ms <= time;
    };
    const auto n = m_notes.size();
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t i = forward ? k : n - 1 - k;
        if (shown(i) && after(i))
            return goTo(int(i));
    }
    return false;
}

QString QcNotesController::timeText(int ms)
{
    return qt(application::qcNoteTimeText(ms));
}

int QcNotesController::parseTime(const QString &text)
{
    static const QRegularExpression time(QStringLiteral(R"(^\s*(?:(\d+):)?(\d{1,2}):(\d{1,2})(?:\.(\d{1,3}))?\s*$)"));
    const auto match = time.match(text);
    if (!match.hasMatch())
        return -1;
    QString fraction = match.captured(4);
    fraction = fraction.leftJustified(3, QLatin1Char('0'));
    const qint64 ms = ((match.captured(1).toLongLong() * 60 + match.captured(2).toLongLong()) * 60 +
                       match.captured(3).toLongLong()) * 1000 + fraction.toLongLong();
    return ms > std::numeric_limits<int>::max() ? -1 : int(ms);
}

void QcNotesController::documentEdited()
{
    const auto *session = target();
    const std::uint64_t serial = session ? session->serial() : 0;
    const std::uint64_t revision = session ? session->revision() : 0;
    const bool canEdit = editable();
    if (m_read && serial == m_readSerial && revision == m_readRevision && canEdit == m_readEditable)
        return;
    if (serial != m_readSerial)
        m_current.reset();
    m_read = true;
    m_readSerial = serial;
    m_readRevision = revision;
    m_readEditable = canEdit;
    read();
    publishMarks();
    emit changed();
}

void QcNotesController::read()
{
    m_notes.clear();
    m_lines.clear();
    m_lineSpansMs.clear();
    m_rows.clear();
    const auto *session = target();
    if (!session)
        return;
    const auto &document = session->document();
    const auto lines = document.lines();
    absl::flat_hash_map<std::uint64_t, std::size_t> rows;
    for (std::size_t i = 0; i < lines.size(); ++i)
        rows.emplace(lines[i]->id.value, i);
    m_notes = application::documentQcNotes(document);
    for (const auto &note : m_notes) {
        const auto line = application::qcNoteLine(lines, note.ms);
        m_lines.push_back(line);
        if (line) {
            const auto *record = lines[rows.at(line->value)];
            m_lineSpansMs.emplace_back(int(record->start.value.microseconds() / 1000),
                                       int(record->end.value.microseconds() / 1000));
            m_rows.push_back(int(rows.at(line->value)) + 1);
        } else {
            m_lineSpansMs.emplace_back(0, 0);
            m_rows.push_back(0);
        }
    }
}

void QcNotesController::publishMarks() const
{
    if (!m_hooks.marks)
        return;
    Marks marks;
    for (std::size_t i = 0; i < m_notes.size(); ++i)
        if (m_lines[i] && shown(i)) {
            const auto &note = m_notes[i];
            marks[m_lines[i]->value].push_back(QCoreApplication::translate("HikariSub.QcNotes", "%1 %2: %3")
                                                   .arg(timeText(int(note.ms)), qt(note.section),
                                                        qt(note.text).replace(QStringLiteral("\\N"), QStringLiteral(" "))));
        }
    const auto index = indexOfCurrent();
    m_hooks.marks(std::move(marks), index && shown(*index) ? m_lines[*index] : std::nullopt);
}

std::optional<std::size_t> QcNotesController::indexOfCurrent() const
{
    if (!m_current)
        return std::nullopt;
    const auto at = std::ranges::find(m_notes, *m_current);
    return at == m_notes.end() ? std::nullopt : std::optional(std::size_t(at - m_notes.begin()));
}

bool QcNotesController::write(std::vector<application::QcNote> notes, const char *step,
                              std::optional<application::QcNote> current)
{
    auto *session = target();
    if (!session || !editable())
        return false;
    for (auto &note : notes)
        note = application::normalisedQcNote(std::move(note));
    std::ranges::stable_sort(notes, {}, &application::QcNote::ms);
    if (notes == m_notes) {
        if (current)
            m_current = std::move(current);
        return true;
    }
    const auto ran = session->run(application::Command{
        step, session->revision(), {}, [&](core::Document &document) {
            return application::setDocumentQcNotes(document, notes);
        }});
    if (!ran)
        return false;
    if (current)
        m_current = std::move(current);
    if (m_hooks.refresh)
        m_hooks.refresh();
    return true;
}

}
