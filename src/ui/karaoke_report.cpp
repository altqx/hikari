#include "karaoke_report.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace hikari::ui {

namespace {

QString duration(double seconds)
{
    if (seconds < 1)
        //: A short duration in milliseconds
        return QCoreApplication::translate("HikariSub.Automation", "%1 ms")
            .arg(QString::number(seconds * 1000, 'f', seconds < 0.01 ? 1 : 0));
    //: A duration in seconds
    return QCoreApplication::translate("HikariSub.Automation", "%1 s").arg(QString::number(seconds, 'f', 2));
}

}

std::optional<KaraokeReport> readKaraokeReport(const std::string &text)
{
    if (text.empty())
        return std::nullopt;
    const QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromStdString(text));
    const QJsonObject o = json.object();
    if (o.value(QLatin1String("report")).toString() != QLatin1String("karaoke-templater") ||
        o.value(QLatin1String("version")).toInt() != 1)
        return std::nullopt;
    KaraokeReport r;
    r.selected = o.value(QLatin1String("mode")).toString() == QLatin1String("selected");
    r.seconds = o.value(QLatin1String("seconds")).toDouble();
    r.prepareSeconds = o.value(QLatin1String("prepare_seconds")).toDouble();
    r.karaokeLines = o.value(QLatin1String("karaoke_lines")).toInt();
    r.generated = o.value(QLatin1String("generated")).toInt();
    r.problems = o.value(QLatin1String("problems")).toInt();
    for (const QJsonValue v : o.value(QLatin1String("templates")).toArray()) {
        const QJsonObject t = v.toObject();
        r.templates.push_back({t.value(QLatin1String("line")).toInt(), t.value(QLatin1String("effect")).toString(),
                               t.value(QLatin1String("style")).toString(), t.value(QLatin1String("seconds")).toDouble(),
                               t.value(QLatin1String("generated")).toInt()});
    }
    return r;
}

QString karaokeReportSummary(const KaraokeReport &r)
{
    //: Status bar after applying karaoke templates; %n is the number of Lines generated, %1 the number of karaoke
    //: Lines templated, %2 the time the run took
    QString text = QCoreApplication::translate("HikariSub.Automation",
                                               "Karaoke template: %n line(s) generated from %1 karaoke line(s) in %2.",
                                               nullptr, r.generated)
                       .arg(r.karaokeLines)
                       .arg(duration(r.seconds));
    if (r.problems > 0)
        //: After the karaoke template status; %n is the number of template problems the log lists
        text += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.Automation",
                                                               "%n template problem(s): see the log.", nullptr,
                                                               r.problems);
    return text;
}

QString karaokeReportDetails(const KaraokeReport &r)
{
    QStringList lines;
    lines << karaokeReportSummary(r);
    //: In the karaoke template report; %1 is the time karaskel took to measure the karaoke lines
    lines << QCoreApplication::translate("HikariSub.Automation", "Measuring the karaoke lines: %1")
                 .arg(duration(r.prepareSeconds));
    std::vector<KaraokeReport::Template> slowest = r.templates;
    std::ranges::stable_sort(slowest, [](const auto &a, const auto &b) { return a.seconds > b.seconds; });
    for (const auto &t : slowest) {
        if (t.effect.startsWith(QLatin1String("code"), Qt::CaseInsensitive))
            //: In the karaoke template report: a code Line's number, its effect and style, and the time it took
            lines << QCoreApplication::translate("HikariSub.Automation", "Code line %1 (%2, %3): %4")
                         .arg(t.line)
                         .arg(t.effect, t.style, duration(t.seconds));
        else
            //: In the karaoke template report: a template Line's number, its effect and style, the time it took and
            //: %n the number of Lines it generated
            lines << QCoreApplication::translate("HikariSub.Automation",
                                                 "Template line %1 (%2, %3): %4, %n line(s) generated", nullptr,
                                                 t.generated)
                         .arg(t.line)
                         .arg(t.effect, t.style, duration(t.seconds));
    }
    return lines.join(QLatin1Char('\n'));
}

}
