// KA7: the Karaoke check window's rows. The checks themselves are
// core::checkKaraoke; here each problem becomes a row the window lists, and
// choosing one selects its Line as a click in the Grid does.

#include "hikari/app/application.h"

#include "hikari/core/karaoke_checks.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace hikari::app {

namespace {

// Centiseconds as seconds, "2.50".
QString seconds(std::int64_t centiseconds)
{
    return QString::number(static_cast<double>(centiseconds) / 100.0, 'f', 2);
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
    const auto *session = targetSession();
    if (!session)
        return rows;
    using Kind = core::KaraokeProblem::Kind;
    for (const core::KaraokeProblem &problem : core::checkKaraoke(session->document())) {
        QString check, detail;
        switch (problem.kind) {
        case Kind::Unclosed:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Unclosed syllable");
            detail = QCoreApplication::translate("HikariSub.Karaoke",
                                                "A karaoke tag has no duration, or its override block is never closed");
            break;
        case Kind::TotalNotDuration:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Syllables don't match the Line");
            detail = QCoreApplication::translate("HikariSub.Karaoke", "The syllables last %1 s; the Line lasts %2 s")
                         .arg(seconds(problem.totalCs), seconds(problem.durationCs));
            break;
        case Kind::ZeroLength:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Zero-length syllable");
            detail = syllables(problem.syllables);
            break;
        case Kind::OutsideLine:
            check = QCoreApplication::translate("HikariSub.Karaoke", "Syllable outside the Line");
            detail = syllables(problem.syllables);
            break;
        }
        rows << QVariantMap{{QStringLiteral("line"), QVariant::fromValue<qulonglong>(problem.line.value)},
                            {QStringLiteral("number"), static_cast<int>(problem.row) + 1},
                            {QStringLiteral("check"), check},
                            {QStringLiteral("detail"), detail}};
    }
    return rows;
}

bool Application::showKaraokeLine(qulonglong id)
{
    const auto *session = targetSession();
    if (!session)
        return false;
    const auto lines = session->document().lines();
    if (std::ranges::none_of(lines, [id](const core::LineRecord *line) { return line->id.value == id; }))
        return false;
    selectLine(id);
    return true;
}

} // namespace hikari::app
