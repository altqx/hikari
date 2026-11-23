#include "hikari/app/reading_problems.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace hikari::app {

namespace {

using application::Problem;
using application::ProblemSeverity;

QString qs(const std::u8string &text)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size()));
}

Problem lineProblem(const core::LineRecord &line, ProblemSeverity severity, std::string code, std::string message)
{
    Problem problem;
    problem.line = line.id;
    problem.severity = severity;
    problem.code = std::move(code);
    problem.message = std::move(message);
    problem.against = line.text;
    return problem;
}

}

std::vector<Problem> readingProblems(const core::Document &document, const application::ReadingLimits &limits,
                                     ui::MeasureOptions measure)
{
    measure.lineLength = limits.lineLength;
    measure.maxLines = limits.maxLines;
    std::vector<Problem> out;
    for (const core::LineRecord *line : document.lines()) {
        if (line->comment)
            continue;
        const ui::LineMeasures measured = ui::measureLine(qs(line->text), document.format(), measure);
        const std::int64_t startMs = line->start.value.microseconds() / 1000;
        const std::int64_t endMs = line->end.value.microseconds() / 1000;
        if (document.format() != core::SubtitleFormat::TMPlayer && endMs > startMs) {
            const int cps = ui::legacyCps(measured.chars, startMs, endMs);
            const bool error = cps >= limits.cpsError;
            if (error || cps >= limits.cpsWarning) {
                const int limit = error ? limits.cpsError : limits.cpsWarning;
                out.push_back(lineProblem(*line, error ? ProblemSeverity::Error : ProblemSeverity::Warning, "cps",
                                          QCoreApplication::translate("HikariSub.QcNotes", "Reads at %1 CPS; the limit is %2.")
                                              .arg(cps)
                                              .arg(limit)
                                              .toStdString()));
            }
        }
        const QStringList wraps = measured.wraps.split(QLatin1Char('/'));
        for (qsizetype i = 0; i < wraps.size(); ++i)
            if (const int length = wraps[i].toInt(); length > limits.lineLength)
                out.push_back(lineProblem(*line, ProblemSeverity::Warning, "line-length",
                                          QCoreApplication::translate("HikariSub.QcNotes", "Line %1 has %2 characters; the limit is %3.")
                                              .arg(i + 1)
                                              .arg(length)
                                              .arg(limits.lineLength)
                                              .toStdString()));
        if (wraps.size() > limits.maxLines)
            out.push_back(lineProblem(*line, ProblemSeverity::Warning, "lines",
                                      QCoreApplication::translate("HikariSub.QcNotes", "%n line(s); the limit is %1.", nullptr,
                                                                  int(wraps.size()))
                                          .arg(limits.maxLines)
                                          .toStdString()));
    }
    if (out.size() > application::Problems::kMostPerDocument)
        out.resize(application::Problems::kMostPerDocument);
    return out;
}

void ReadingProblems::setSettings(const Settings &settings)
{
    if (settings == m_settings)
        return;
    m_settings = settings;
    for (auto &[document, seen] : m_seen)
        seen.revision.reset();
    if (!settings.shown) {
        m_problems.clear(std::string(kSource), std::nullopt, std::nullopt);
        m_seen.clear();
    }
}

void ReadingProblems::sync(const std::vector<std::pair<application::DocumentId, const application::EditSession *>> &open)
{
    std::erase_if(m_seen, [&](const auto &entry) {
        return std::ranges::none_of(open, [&](const auto &o) { return o.first.value == entry.first; });
    });
    if (!m_settings.shown)
        return;
    for (const auto &[id, session] : open) {
        if (!session)
            continue;
        Seen &seen = m_seen[id.value];
        if (seen.revision == session->revision())
            continue;
        seen.revision = session->revision();
        const core::Document &document = session->document();
        std::vector<Problem> problems =
            readingProblems(document, application::readingLimits(document, m_settings.profile),
                            ui::MeasureOptions{.allCharsForCps = m_settings.allCharsForCps,
                                               .allCharsForWraps = m_settings.allCharsForWraps});
        if (problems == seen.problems)
            continue;
        seen.problems = problems;
        m_problems.set("hikari", std::string(kSource),
                       QCoreApplication::translate("HikariSub.QcNotes", "Reading").toStdString(), id, "default",
                       std::move(problems));
    }
}

}
