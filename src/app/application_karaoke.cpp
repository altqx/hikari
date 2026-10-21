// KA7: the Karaoke check window's rows. The checks themselves are
// core::checkKaraoke; here each problem becomes a row the window lists, and
// choosing one selects its Line as a click in the Grid does, while the
// Document checked is still the editing target.

#include "hikari/app/application.h"

#include "hikari/core/karaoke_checks.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace hikari::app {

namespace {

// Milliseconds as seconds: "2.50", or "2.505" for a fractional centisecond.
QString seconds(std::int64_t ms)
{
    return QString::number(static_cast<double>(ms) / 1000.0, 'f', ms % 10 != 0 ? 3 : 2);
}

QString syllables(const std::vector<std::size_t> &numbers)
{
    QStringList list;
    for (std::size_t n : numbers)
        list << QString::number(n);
    const QString label = numbers.size() == 1 ? QCoreApplication::translate("HikariSub.Karaoke", "Syllable %1")
                                              : QCoreApplication::translate("HikariSub.Karaoke", "Syllables %1");
    return label.arg(list.join(QStringLiteral(", ")));
}

} // namespace

QVariantList Application::checkKaraoke() const
{
    QVariantList rows;
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return rows;
    using Kind = core::KaraokeProblem::Kind;
    // The editor's pending draft counts, as it shows on the video: the
    // Document with the draft applied (a draft is not committed for this).
    const auto draft = m_editorDocument == target ? m_editor->draftDocument() : std::nullopt;
    for (const core::KaraokeProblem &problem : core::checkKaraoke(draft ? *draft : session->document())) {
        QString check, detail;
        switch (problem.kind) {
        case Kind::Unclosed:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Unclosed syllable");
            detail = QCoreApplication::translate("HikariSub.Karaoke",
                                                "A karaoke tag has no duration, or its override block is never closed");
            break;
        case Kind::TotalNotDuration:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Syllables don't match the line");
            detail = QCoreApplication::translate("HikariSub.Karaoke", "The syllables last %1 s; the line lasts %2 s")
                         .arg(seconds(problem.totalMs), seconds(problem.durationMs));
            break;
        case Kind::ZeroLength:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Zero-length syllable");
            detail = syllables(problem.syllables);
            break;
        case Kind::OutsideLine:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Syllable outside the line");
            detail = syllables(problem.syllables);
            break;
        }
        rows << QVariantMap{{QStringLiteral("document"), QVariant::fromValue<qulonglong>(target->value)},
                            {QStringLiteral("line"), QVariant::fromValue<qulonglong>(problem.line.value)},
                            {QStringLiteral("number"), static_cast<int>(problem.row) + 1},
                            {QStringLiteral("check"), check},
                            {QStringLiteral("detail"), detail}};
    }
    return rows;
}

bool Application::showKaraokeLine(qulonglong document, qulonglong line)
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target && target->value == document ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const auto lines = session->document().lines();
    if (std::ranges::none_of(lines, [line](const core::LineRecord *record) { return record->id.value == line; }))
        return false;
    // Refused when the editor cannot leave its Line (a draft it cannot commit).
    return applySelection(gridSelection().plain(session->selection(), core::LineId{line}));
}

} // namespace hikari::app
