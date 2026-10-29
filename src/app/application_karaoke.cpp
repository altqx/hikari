#include "hikari/app/application.h"

#include "hikari/core/karaoke_checks.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace hikari::app {

namespace {

QString seconds(std::int64_t ms)
{
    return QString::number(static_cast<double>(ms) / 1000.0, 'f', ms % 10 != 0 ? 3 : 2);
}

QString syllables(const core::KaraokeProblem &problem)
{
    QStringList list;
    for (std::size_t n : problem.syllables)
        list << QString::number(n);
    if (problem.syllableCount > problem.syllables.size())
        list << QString(QChar(0x2026));
    const QString label = problem.syllableCount == 1 ? QCoreApplication::translate("HikariSub.Karaoke", "Syllable %1")
                                                     : QCoreApplication::translate("HikariSub.Karaoke", "Syllables %1");
    return label.arg(list.join(QStringLiteral(", ")));
}

}

QVariantMap Application::checkKaraoke() const
{
    QVariantList rows;
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return {{QStringLiteral("rows"), rows}, {QStringLiteral("unlisted"), 0}};
    using Kind = core::KaraokeProblem::Kind;
    const auto draft = m_editorDocument == target ? m_editor->draftDocument() : std::nullopt;
    const core::KaraokeCheck checked = core::checkKaraoke(draft ? *draft : session->document());
    for (const core::KaraokeProblem &problem : checked.problems) {
        QString check, detail;
        switch (problem.kind) {
        case Kind::Unclosed:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Unclosed syllable");
            detail = QCoreApplication::translate("HikariSub.Karaoke",
                                                "A karaoke tag has no duration, or its override block is never closed");
            break;
        case Kind::NotForAudioEditor:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Not read by the audio editor");
            detail = QCoreApplication::translate(
                "HikariSub.Karaoke", "No karaoke tag's duration starts with a digit, as in \\k50, so the audio editor splits the line anew");
            break;
        case Kind::EditorOnly:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Other syllables in the audio editor");
            detail = QCoreApplication::translate(
                "HikariSub.Karaoke",
                "The audio editor splits at every \\k tag and reads its number its own way, so a tag such as \\keep, "
                "\\kfo50 or \\k(50) gives it syllables unlike playback's");
            break;
        case Kind::TotalNotDuration:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Syllables don't match the line");
            detail = QCoreApplication::translate("HikariSub.Karaoke", "The syllables last %1 s; the line lasts %2 s")
                         .arg(seconds(problem.totalMs), seconds(problem.durationMs));
            break;
        case Kind::ZeroLength:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Zero-length syllable");
            detail = syllables(problem);
            break;
        case Kind::OutsideLine:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Syllable outside the line");
            detail = syllables(problem);
            break;
        }
        rows << QVariantMap{{QStringLiteral("document"), QVariant::fromValue<qulonglong>(target->value)},
                            {QStringLiteral("line"), QVariant::fromValue<qulonglong>(problem.line.value)},
                            {QStringLiteral("number"), static_cast<int>(problem.row) + 1},
                            {QStringLiteral("check"), check},
                            {QStringLiteral("detail"), detail}};
    }
    return {{QStringLiteral("rows"), rows}, {QStringLiteral("unlisted"), QVariant::fromValue<qulonglong>(checked.unlisted)}};
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
    return applySelection(gridSelection().plain(session->selection(), core::LineId{line}));
}

}
