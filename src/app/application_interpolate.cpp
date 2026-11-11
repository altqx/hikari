#include "hikari/app/application.h"
#include "hikari/application/interpolate.h"
#include "hikari/application/tag_lerp.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <map>

namespace hikari::app {

namespace {

QString qstring(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

application::interpolate::Options optionsOf(const QVariantMap &o)
{
    application::interpolate::Options out;
    out.accel = std::clamp(o.value(QStringLiteral("accel"), 1.0).toDouble(), 0.01, 10.0);
    out.shortestRotation = o.value(QStringLiteral("shortestRotation")).toBool();
    out.cascade = o.value(QStringLiteral("cascade")).toBool();
    for (const QString &id : o.value(QStringLiteral("excluded")).toStringList())
        out.excluded.push_back(id.toStdString());
    return out;
}

QString problemText(application::interpolate::Problem problem)
{
    switch (problem) {
    case application::interpolate::Problem::TooFewLines:
        return QCoreApplication::translate("HikariSub.Interpolate", "Select at least three lines: the first, the last and the lines between them.");
    case application::interpolate::Problem::NothingChanges:
        return QCoreApplication::translate("HikariSub.Interpolate", "The first and last lines have the same tags, so there’s nothing to interpolate.");
    }
    return {};
}

QString skipText(application::interpolate::Skip skip)
{
    using application::interpolate::Skip;
    switch (skip) {
    case Skip::None:
        return {};
    case Skip::DifferentShapes:
        return QCoreApplication::translate("HikariSub.Interpolate", "The clips have different shapes.");
    case Skip::RectAndDrawing:
        return QCoreApplication::translate("HikariSub.Interpolate", "One clip is a rectangle and the other a drawing.");
    case Skip::OneSidedClip:
        return QCoreApplication::translate("HikariSub.Interpolate", "Only one of the two lines has this clip.");
    case Skip::DifferentTransforms:
        return QCoreApplication::translate("HikariSub.Interpolate", "The transforms have different shapes.");
    }
    return {};
}

QString valueOf(const std::string &tag, const std::string &name)
{
    return tag.size() > name.size() ? qstring(std::string_view(tag).substr(1 + name.size())) : QString();
}

QString swatch(const QString &value)
{
    QString hex = value;
    hex.remove(QLatin1Char('&')).remove(QLatin1Char('H')).remove(QLatin1Char('h'));
    bool ok = false;
    const uint bgr = hex.toUInt(&ok, 16);
    if (!ok)
        return {};
    return QStringLiteral("#%1%2%3")
        .arg(bgr & 0xFF, 2, 16, QLatin1Char('0'))
        .arg((bgr >> 8) & 0xFF, 2, 16, QLatin1Char('0'))
        .arg((bgr >> 16) & 0xFF, 2, 16, QLatin1Char('0'))
        .toUpper();
}

}

QVariantMap Application::interpolationPlan(const QVariantMap &o)
{
    m_interpolationPreview.clear();
    auto *session = targetSession();
    if (!session) {
        showVideoSubtitles();
        return {};
    }
    const auto run = application::interpolate::selectedRun(*session, shownLines());
    const auto plan = application::interpolate::plan(run.lines, optionsOf(o));
    QVariantMap out{{QStringLiteral("count"), static_cast<int>(run.ids.size())}};
    std::map<core::LineId, int> rows;
    int row = 0;
    for (const auto *l : session->document().lines())
        rows.emplace(l->id, ++row);
    if (!run.ids.empty()) {
        out.insert(QStringLiteral("firstRow"), rows[run.ids.front()]);
        out.insert(QStringLiteral("lastRow"), rows[run.ids.back()]);
    }
    if (!plan) {
        out.insert(QStringLiteral("problem"), problemText(plan.error()));
        showVideoSubtitles();
        return out;
    }
    QVariantList changes;
    for (const auto &c : plan->changes) {
        const auto *spec = application::lerp::tagSpec(c.key);
        const bool colour = spec && spec->kind == application::lerp::Kind::Colour;
        const QString from = valueOf(c.from, c.name), to = valueOf(c.to, c.name);
        changes.push_back(QVariantMap{{QStringLiteral("id"), qstring(c.id)},
                                      {QStringLiteral("tag"), QStringLiteral("\\") + qstring(c.name)},
                                      {QStringLiteral("before"), qstring(c.before)},
                                      {QStringLiteral("from"), from},
                                      {QStringLiteral("to"), to},
                                      {QStringLiteral("fromColour"), colour ? swatch(from) : QString()},
                                      {QStringLiteral("toColour"), colour ? swatch(to) : QString()},
                                      {QStringLiteral("skip"), skipText(c.skip)}});
    }
    QVariantList results;
    for (std::size_t i = 0; i < run.ids.size(); ++i) {
        if (plan->texts[i] == run.lines[i].text)
            continue;
        m_interpolationPreview.emplace(run.ids[i],
                                       std::u8string(plan->texts[i].begin(), plan->texts[i].end()));
        results.push_back(QVariantMap{{QStringLiteral("row"), rows[run.ids[i]]},
                                      {QStringLiteral("text"), qstring(plan->texts[i])}});
    }
    out.insert(QStringLiteral("changes"), changes);
    out.insert(QStringLiteral("results"), results);
    out.insert(QStringLiteral("startBlockOnly"), plan->startBlockOnly);
    showVideoSubtitles();
    return out;
}

QString Application::applyInterpolation(const QVariantMap &o)
{
    clearInterpolationPreview();
    auto *session = targetSession();
    if (!session)
        return problemText(application::interpolate::Problem::TooFewLines);
    const auto run = application::interpolate::selectedRun(*session, shownLines());
    const auto plan = application::interpolate::plan(run.lines, optionsOf(o));
    if (!plan)
        return problemText(plan.error());
    const bool done = runFilter(
        [&](application::EditSession &s) { return application::interpolate::applyPlan(s, run, *plan); });
    return done ? QString() : QCoreApplication::translate("HikariSub.Interpolate", "Nothing would change with these tags.");
}

void Application::clearInterpolationPreview()
{
    if (m_interpolationPreview.empty())
        return;
    m_interpolationPreview.clear();
    showVideoSubtitles();
}

}
