#include "hikari/app/application.h"
#include "hikari/application/timing_helpers.h"

#include <QStringList>
#include <QVariantList>

#include <algorithm>

namespace hikari::app {

namespace {

using application::TimingEdge;
using application::TimingReason;
using application::TimingToward;

constexpr int kStripMarginMs = 1500;

QString reasonName(TimingReason reason)
{
    switch (reason) {
    case TimingReason::Kept: return QStringLiteral("kept");
    case TimingReason::OnKeyframe: return QStringLiteral("onKeyframe");
    case TimingReason::KeyframeBefore: return QStringLiteral("keyframeBefore");
    case TimingReason::KeyframeAfter: return QStringLiteral("keyframeAfter");
    case TimingReason::Linked: return QStringLiteral("linked");
    case TimingReason::LeadIn: return QStringLiteral("leadIn");
    case TimingReason::LeadOut: return QStringLiteral("leadOut");
    }
    return {};
}

bool styleChosen(const application::ShiftTimesSettings &settings, const core::LineRecord &line)
{
    if (settings.whichLines != 5)
        return true;
    const auto styles = QString::fromUtf8(reinterpret_cast<const char *>(settings.styles.data()),
                                          static_cast<qsizetype>(settings.styles.size()))
                            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    const auto style = QString::fromUtf8(reinterpret_cast<const char *>(line.style.data()),
                                         static_cast<qsizetype>(line.style.size()));
    return std::any_of(styles.begin(), styles.end(), [&](const QString &s) { return s.trimmed() == style; });
}

}

bool Application::snapToAdjacentKeyframe(bool end, bool next)
{
    auto *session = targetSession();
    if (!session || !exactTimebase())
        return false;
    const auto &video = activeMedia().video->session();
    const auto keyframes = video.keyframes();
    const bool done = application::snapToAdjacentKeyframe(*session, video.legacyTimebase(), keyframes,
                                                          end ? TimingEdge::End : TimingEdge::Start,
                                                          next ? TimingToward::Next : TimingToward::Previous)
                          .has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::changeLead(bool end, bool add)
{
    auto *session = targetSession();
    if (!session || !exactTimebase())
        return false;
    const int amount = static_cast<int>(m_settings->integer(end ? "audio.leadOutValue" : "audio.leadInValue"));
    const bool done = application::changeLead(*session, activeMedia().video->session().legacyTimebase(),
                                              end ? TimingEdge::End : TimingEdge::Start, add, amount)
                          .has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::nudgeEdge(bool end, int frames)
{
    auto *session = targetSession();
    if (!session || !exactTimebase())
        return false;
    const bool done = application::nudgeEdgeByFrames(*session, activeMedia().video->session().legacyTimebase(),
                                                     end ? TimingEdge::End : TimingEdge::Start, frames)
                          .has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::extendNeighbour(bool next)
{
    auto *session = targetSession();
    if (!session)
        return false;
    const bool done =
        application::extendNeighbour(*session, next ? TimingToward::Next : TimingToward::Previous).has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

QVariantMap Application::timingSuggestion(int shellWindow)
{
    QVariantMap out{{QStringLiteral("ok"), false}};
    auto *session = windowSession(shellWindow);
    if (!session || !session->selection().active || !exactTimebase()) {
        out.insert(QStringLiteral("problem"), QStringLiteral("noLine"));
        return out;
    }
    application::LegacyTimebase timebase;
    const auto context = shiftContext(timebase);
    const auto settings = shiftRunSettings(QStringLiteral("postprocessor"));
    const auto active = *session->selection().active;
    const auto lines = session->document().lines();
    int index = 0, count = 0;
    const core::LineRecord *line = nullptr;
    for (const auto *l : lines) {
        if (l->comment || !styleChosen(settings, *l))
            continue;
        ++count;
        if (l->id == active) {
            index = count;
            line = l;
        }
    }
    if (!line) {
        out.insert(QStringLiteral("problem"), QStringLiteral("skipped"));
        out.insert(QStringLiteral("count"), count);
        return out;
    }
    const auto s = application::suggestTiming(session->document(), active, timebase, context.keyframes, settings);
    if (!s)
        return out;
    out = {{QStringLiteral("ok"), true},
           {QStringLiteral("line"), QVariant::fromValue(static_cast<qulonglong>(active.value))},
           {QStringLiteral("index"), index},
           {QStringLiteral("count"), count},
           {QStringLiteral("style"), QString::fromUtf8(reinterpret_cast<const char *>(line->style.data()),
                                                        static_cast<qsizetype>(line->style.size()))},
           {QStringLiteral("text"), QString::fromUtf8(reinterpret_cast<const char *>(line->text.data()),
                                                       static_cast<qsizetype>(line->text.size()))},
           {QStringLiteral("startBefore"), s->startBefore},
           {QStringLiteral("endBefore"), s->endBefore},
           {QStringLiteral("start"), s->startMs},
           {QStringLiteral("end"), s->endMs},
           {QStringLiteral("startReason"), reasonName(s->startReason)},
           {QStringLiteral("endReason"), reasonName(s->endReason)},
           {QStringLiteral("startFrames"), s->startFrames},
           {QStringLiteral("endFrames"), s->endFrames},
           {QStringLiteral("linked"), s->linked.has_value()},
           {QStringLiteral("linkedEndBefore"), s->linkedEndBefore},
           {QStringLiteral("leadIn"), settings.leadIn},
           {QStringLiteral("leadOut"), settings.leadOut}};
    QVariantList near;
    for (const int kf : context.keyframes) {
        const int ms = (timebase.startTimeFor(kf) / 10) * 10;
        if (ms >= s->startBefore - kStripMarginMs && ms <= s->endBefore + kStripMarginMs)
            near << ms;
    }
    out.insert(QStringLiteral("keyframes"), near);
    out.insert(QStringLiteral("windowStart"), std::max(0, std::min(s->startMs, s->startBefore) - kStripMarginMs));
    out.insert(QStringLiteral("windowEnd"), std::max(s->endMs, s->endBefore) + kStripMarginMs);
    return out;
}

bool Application::acceptTimingSuggestion(int shellWindow, int startMs, int endMs)
{
    auto *session = windowSession(shellWindow);
    if (!session || !session->selection().active || !exactTimebase())
        return false;
    application::LegacyTimebase timebase;
    const auto context = shiftContext(timebase);
    auto s = application::suggestTiming(session->document(), *session->selection().active, timebase, context.keyframes,
                                        shiftRunSettings(QStringLiteral("postprocessor")));
    if (!s)
        return false;
    if (startMs >= 0)
        s->startMs = startMs;
    if (endMs >= 0)
        s->endMs = endMs;
    if (!application::acceptTiming(*session, *s))
        return false;
    editor().reloadFromSession();
    refreshViews();
    skipTimingSuggestion(shellWindow);
    return true;
}

bool Application::skipTimingSuggestion(int shellWindow)
{
    auto *session = windowSession(shellWindow);
    if (!session || !session->selection().active)
        return false;
    const auto settings = shiftRunSettings(QStringLiteral("postprocessor"));
    const auto lines = session->document().lines();
    const auto at = std::find_if(lines.begin(), lines.end(),
                                 [&](const core::LineRecord *l) { return l->id == *session->selection().active; });
    if (at == lines.end())
        return false;
    const auto next = std::find_if(at + 1, lines.end(),
                                   [&](const core::LineRecord *l) { return !l->comment && styleChosen(settings, *l); });
    if (next == lines.end())
        return false;
    return applySelection(gridSelection().plain(session->selection(), (*next)->id));
}

}
