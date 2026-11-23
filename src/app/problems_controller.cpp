#include "hikari/app/problems_controller.h"
#include "hikari/app/reading_problems.h"

#include "hikari/core/text_projection.h"

#include <QCoreApplication>

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>

namespace hikari::app {

namespace {

using application::ProblemSeverity;

constexpr int kNoRow = std::numeric_limits<int>::max();
constexpr std::array<ProblemSeverity, 4> kSeverities{ProblemSeverity::Error, ProblemSeverity::Warning,
                                                     ProblemSeverity::Info, ProblemSeverity::Hint};

QString qt(std::string_view text)
{
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

QString qt(std::u8string_view text)
{
    return QString::fromStdU16String(core::toUtf16(text));
}

QString severityId(ProblemSeverity severity)
{
    switch (severity) {
    case ProblemSeverity::Error: return QStringLiteral("error");
    case ProblemSeverity::Warning: return QStringLiteral("warning");
    case ProblemSeverity::Info: return QStringLiteral("info");
    case ProblemSeverity::Hint: break;
    }
    return QStringLiteral("hint");
}

QString severityName(ProblemSeverity severity)
{
    switch (severity) {
    case ProblemSeverity::Error: return QCoreApplication::translate("HikariSub.QcNotes", "Errors");
    case ProblemSeverity::Warning: return QCoreApplication::translate("HikariSub.QcNotes", "Warnings");
    case ProblemSeverity::Info: return QCoreApplication::translate("HikariSub.QcNotes", "Info");
    case ProblemSeverity::Hint: break;
    }
    return QCoreApplication::translate("HikariSub.QcNotes", "Hints");
}

application::MarkColour markColour(ProblemSeverity severity)
{
    switch (severity) {
    case ProblemSeverity::Error: return application::MarkColour::Danger;
    case ProblemSeverity::Warning: return application::MarkColour::Warning;
    case ProblemSeverity::Info: return application::MarkColour::Accent;
    case ProblemSeverity::Hint: break;
    }
    return application::MarkColour::Muted;
}

int roleOf(application::ProblemField field)
{
    return field == application::ProblemField::Translation ? 1 : 0;
}

bool current(const application::HeldProblem &held, const core::LineRecord *line)
{
    return line && application::problemFieldText(*line, held.problem.field) == held.problem.against;
}

absl::flat_hash_map<std::uint64_t, std::size_t> rowsOf(const core::Document &document)
{
    absl::flat_hash_map<std::uint64_t, std::size_t> rows;
    for (const core::LineRecord *line : document.lines())
        rows.emplace(line->id.value, rows.size());
    return rows;
}

QStringList titlesOf(const application::Problem &problem)
{
    QStringList out;
    for (const application::ProblemFix &fix : problem.fixes)
        out << qt(fix.title);
    return out;
}

QString preview(const core::LineRecord &line)
{
    QString text = qt(line.text).replace(QStringLiteral("\\N"), QStringLiteral(" "));
    constexpr qsizetype kLongest = 60;
    return text.size() > kLongest ? text.left(kLongest) + QChar(0x2026) : text;
}

}

void addProblemMarks(ui::LineTableModel::RowMarks &marks, const application::Problems &problems,
                     application::DocumentId id, const core::Document &document)
{
    absl::flat_hash_map<std::uint64_t, const core::LineRecord *> lines;
    for (const core::LineRecord *line : document.lines())
        lines.emplace(line->id.value, line);
    absl::flat_hash_map<std::uint64_t, std::array<int, 4>> counts;
    for (const application::HeldProblem &held : problems.of(id)) {
        if (held.source == ReadingProblems::kSource)
            continue;
        const auto line = lines.find(held.problem.line.value);
        if (line != lines.end() && current(held, line->second))
            ++counts[held.problem.line.value][std::size_t(held.problem.severity)];
    }
    for (const auto &[line, count] : counts) {
        QStringList parts;
        std::optional<ProblemSeverity> worst;
        int total = 0;
        for (const ProblemSeverity severity : kSeverities) {
            const int n = count[std::size_t(severity)];
            if (n == 0)
                continue;
            worst = worst.value_or(severity);
            total += n;
            switch (severity) {
            case ProblemSeverity::Error: parts << QCoreApplication::translate("HikariSub.Grid", "%n error(s)", nullptr, n); break;
            case ProblemSeverity::Warning: parts << QCoreApplication::translate("HikariSub.Grid", "%n warning(s)", nullptr, n); break;
            case ProblemSeverity::Info: parts << QCoreApplication::translate("HikariSub.Grid", "%n info", nullptr, n); break;
            case ProblemSeverity::Hint: parts << QCoreApplication::translate("HikariSub.Grid", "%n hint(s)", nullptr, n); break;
            }
        }
        const QString note = QCoreApplication::translate("HikariSub.Grid", "%n problem(s): %1",
                                                         "A Grid row's problems: how many, then of each severity", total)
                                 .arg(parts.join(QStringLiteral(", ")));
        auto &row = marks[line];
        row.insert(row.begin(), ui::LineTableModel::RowMark{markColour(*worst), note});
    }
}

ProblemsController::ProblemsController(application::Problems &problems, QcNotesController &notes, Hooks hooks,
                                       QObject *parent)
    : QObject(parent), m_problems(problems), m_notes(notes), m_hooks(std::move(hooks))
{
    connect(&m_notes, &QcNotesController::changed, this, [this] {
        rebuild();
        emit changed();
    });
}

void ProblemsController::setSource(const QString &source)
{
    if (source == m_source)
        return;
    m_source = source;
    rebuild();
    emit changed();
}

void ProblemsController::setSeverity(const QString &severity)
{
    if (severity == m_severity)
        return;
    m_severity = severity;
    rebuild();
    emit changed();
}

const ProblemsController::Held *ProblemsController::find(const QString &id) const
{
    const std::string wanted = id.toStdString();
    const auto it = std::ranges::find(m_held, wanted, [](const Held &h) -> const std::string & { return h.held.id; });
    return it == m_held.end() ? nullptr : &*it;
}

bool ProblemsController::goTo(const QString &id)
{
    const Held *held = find(id);
    const auto *session = m_hooks.session ? m_hooks.session() : nullptr;
    if (!held || !session)
        return false;
    const core::LineId line = held->held.problem.line;
    if (m_hooks.selectLine)
        m_hooks.selectLine(line);
    for (const core::LineRecord *record : session->document().lines())
        if (record->id == line && m_hooks.seek)
            m_hooks.seek(int(record->start.value.microseconds() / 1000));
    return true;
}

QStringList ProblemsController::fixesOf(const QString &id) const
{
    const Held *held = find(id);
    return held ? titlesOf(held->held.problem) : QStringList();
}

bool ProblemsController::applyFix(const QString &id, int fix)
{
    const Held *held = find(id);
    const auto document = m_hooks.document ? m_hooks.document() : std::nullopt;
    if (!held || !document || fix < 0 || std::size_t(fix) >= held->held.problem.fixes.size() || !m_hooks.fix)
        return false;
    const std::string problem = held->held.id;
    return m_hooks.fix(*document, problem, fix);
}

QVariantList ProblemsController::editorMarks(int role) const
{
    QVariantList out;
    const auto *session = m_hooks.session ? m_hooks.session() : nullptr;
    const auto active = session ? session->selection().active : std::nullopt;
    const auto raw = active && m_hooks.editorRaw ? m_hooks.editorRaw(role) : std::nullopt;
    if (!raw)
        return out;
    for (const Held &h : m_held) {
        const application::Problem &p = h.held.problem;
        if (p.line != *active || roleOf(p.field) != role || !p.from || core::toUtf16(p.against) != *raw)
            continue;
        const auto [from, to] = m_hooks.displaySpan ? m_hooks.displaySpan(role, int(*p.from), int(*p.to))
                                                    : std::pair{int(*p.from), int(*p.to)};
        if (from >= to)
            continue;
        out.push_back(QVariantMap{{QStringLiteral("from"), from},
                                  {QStringLiteral("to"), to},
                                  {QStringLiteral("severity"), severityId(p.severity)},
                                  {QStringLiteral("message"), qt(p.message)},
                                  {QStringLiteral("source"), qt(m_problems.name(h.held.source))}});
    }
    return out;
}

QVariantList ProblemsController::editorFixesAt(int role, int position) const
{
    QVariantList out;
    const auto *session = m_hooks.session ? m_hooks.session() : nullptr;
    const auto active = session ? session->selection().active : std::nullopt;
    const auto raw = active && m_hooks.editorRaw ? m_hooks.editorRaw(role) : std::nullopt;
    if (!raw)
        return out;
    const auto add = [&](const Held &h) {
        for (std::size_t f = 0; f < h.held.problem.fixes.size(); ++f)
            out.push_back(QVariantMap{{QStringLiteral("problem"), qt(h.held.id)},
                                      {QStringLiteral("fix"), int(f)},
                                      {QStringLiteral("title"), qt(h.held.problem.fixes[f].title)},
                                      {QStringLiteral("source"), qt(m_problems.name(h.held.source))}});
    };
    std::vector<const Held *> whole;
    for (const Held &h : m_held) {
        const application::Problem &p = h.held.problem;
        if (p.line != *active || roleOf(p.field) != role || core::toUtf16(p.against) != *raw)
            continue;
        if (!p.from) {
            whole.push_back(&h);
            continue;
        }
        const auto [from, to] = m_hooks.displaySpan ? m_hooks.displaySpan(role, int(*p.from), int(*p.to))
                                                    : std::pair{int(*p.from), int(*p.to)};
        if (from <= position && position <= to)
            add(h);
    }
    for (const Held *h : whole)
        add(*h);
    return out;
}

void ProblemsController::documentEdited()
{
    const auto document = m_hooks.document ? m_hooks.document() : std::nullopt;
    const auto *session = m_hooks.session ? m_hooks.session() : nullptr;
    const std::uint64_t revision = session ? session->revision() : 0;
    if (m_read && document == m_readDocument && revision == m_readRevision)
        return;
    m_read = true;
    m_readDocument = document;
    m_readRevision = revision;
    read();
    rebuild();
    emit changed();
}

void ProblemsController::problemsChanged(application::DocumentId document)
{
    if (!m_hooks.document || m_hooks.document() != document)
        return;
    read();
    rebuild();
    emit changed();
}

void ProblemsController::read()
{
    m_held.clear();
    const auto document = m_hooks.document ? m_hooks.document() : std::nullopt;
    const auto *session = m_hooks.session ? m_hooks.session() : nullptr;
    if (!document || !session)
        return;
    const auto lines = session->document().lines();
    const auto rows = rowsOf(session->document());
    for (application::HeldProblem &held : m_problems.of(*document)) {
        const auto row = rows.find(held.problem.line.value);
        if (row == rows.end() || !current(held, lines[row->second]))
            continue;
        m_held.push_back(Held{std::move(held), int(row->second) + 1});
    }
}

void ProblemsController::rebuild()
{
    m_entries.clear();
    m_sources.clear();
    m_severities.clear();
    const auto *session = m_hooks.session ? m_hooks.session() : nullptr;
    const std::vector<const core::LineRecord *> lines = session ? session->document().lines()
                                                                : std::vector<const core::LineRecord *>{};
    const auto groupOf = [&](int row) {
        if (row <= 0 || std::size_t(row) > lines.size())
            return QCoreApplication::translate("HikariSub.QcNotes", "No line");
        return QCoreApplication::translate("HikariSub.QcNotes", "Line %1 · %2").arg(row).arg(preview(*lines[std::size_t(row - 1)]));
    };

    const QVariantList notes = m_notes.notes();
    std::array<int, 4> severityCounts{};
    severityCounts[std::size_t(ProblemSeverity::Info)] = int(notes.size());
    std::vector<std::pair<std::string, int>> sourceCounts;
    for (const Held &h : m_held) {
        ++severityCounts[std::size_t(h.held.problem.severity)];
        auto it = std::ranges::find(sourceCounts, h.held.source, &std::pair<std::string, int>::first);
        if (it == sourceCounts.end())
            it = sourceCounts.insert(sourceCounts.end(), {h.held.source, 0});
        ++it->second;
    }
    const QString qc = qt(application::Problems::kQcSource);
    if (!notes.isEmpty())
        m_sources.push_back(QVariantMap{{QStringLiteral("id"), qc},
                                        {QStringLiteral("name"), QCoreApplication::translate("HikariSub.QcNotes", "QC notes")},
                                        {QStringLiteral("count"), int(notes.size())}});
    for (const auto &[source, count] : sourceCounts)
        m_sources.push_back(QVariantMap{{QStringLiteral("id"), qt(source)},
                                        {QStringLiteral("name"), qt(m_problems.name(source))},
                                        {QStringLiteral("count"), count}});
    for (const ProblemSeverity severity : kSeverities)
        if (const int count = severityCounts[std::size_t(severity)])
            m_severities.push_back(QVariantMap{{QStringLiteral("id"), severityId(severity)},
                                               {QStringLiteral("name"), severityName(severity)},
                                               {QStringLiteral("count"), count}});
    if (!m_source.isEmpty() && std::ranges::none_of(m_sources, [&](const QVariant &s) {
            return s.toMap().value(QStringLiteral("id")).toString() == m_source;
        }))
        m_source.clear();
    if (!m_severity.isEmpty() && std::ranges::none_of(m_severities, [&](const QVariant &s) {
            return s.toMap().value(QStringLiteral("id")).toString() == m_severity;
        }))
        m_severity.clear();

    struct Entry {
        int row;
        int kind;
        qint64 at;
        QVariantMap fields;
    };
    std::vector<Entry> entries;
    const QString info = severityId(ProblemSeverity::Info);
    if ((m_source.isEmpty() || m_source == qc) && (m_severity.isEmpty() || m_severity == info))
        for (const QVariant &value : notes) {
            QVariantMap note = value.toMap();
            const int row = note.value(QStringLiteral("row")).toInt();
            note.insert(QStringLiteral("key"), QStringLiteral("note:%1").arg(note.value(QStringLiteral("index")).toInt()));
            note.insert(QStringLiteral("kind"), QStringLiteral("note"));
            note.insert(QStringLiteral("group"), groupOf(row));
            note.insert(QStringLiteral("severity"), info);
            entries.push_back(Entry{row > 0 ? row : kNoRow, 0, note.value(QStringLiteral("time")).toLongLong(), std::move(note)});
        }
    for (const Held &h : m_held) {
        const application::Problem &p = h.held.problem;
        const QString source = qt(h.held.source);
        const QString severity = severityId(p.severity);
        if ((!m_source.isEmpty() && m_source != source) || (!m_severity.isEmpty() && m_severity != severity))
            continue;
        entries.push_back(Entry{h.row, 1, qint64(p.from.value_or(0)),
                                QVariantMap{{QStringLiteral("key"), qt(h.held.id)},
                                            {QStringLiteral("kind"), QStringLiteral("problem")},
                                            {QStringLiteral("group"), groupOf(h.row)},
                                            {QStringLiteral("row"), h.row},
                                            {QStringLiteral("severity"), severity},
                                            {QStringLiteral("id"), qt(h.held.id)},
                                            {QStringLiteral("message"), qt(p.message)},
                                            {QStringLiteral("source"), source},
                                            {QStringLiteral("sourceName"), qt(m_problems.name(h.held.source))},
                                            {QStringLiteral("field"), roleOf(p.field) == 1 ? QStringLiteral("translation")
                                                                                          : QStringLiteral("text")},
                                            {QStringLiteral("code"), qt(p.code)},
                                            {QStringLiteral("fixes"), titlesOf(p)}}});
    }
    std::ranges::stable_sort(entries, [](const Entry &a, const Entry &b) {
        return std::tie(a.row, a.kind, a.at) < std::tie(b.row, b.kind, b.at);
    });
    for (Entry &entry : entries)
        m_entries.push_back(std::move(entry.fields));
}

}
