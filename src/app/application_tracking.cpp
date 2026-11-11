#include "hikari/app/application.h"
#include "hikari/application/perspective_tracking.h"
#include "hikari/application/resample.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>

namespace hikari::app {

namespace persp = application::visual::perspective;

namespace {

std::optional<application::PowerPin> powerPin(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    return application::parsePowerPin(
        std::u8string_view(reinterpret_cast<const char8_t *>(utf8.constData()), static_cast<std::size_t>(utf8.size())));
}

}

QVariantMap Application::perspectiveTrackingValues() const
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    if (!session || video.state() != application::VideoSession::State::Ready)
        return {};
    const auto timebase = video.legacyTimebase();
    const auto shown = shownLines();
    const auto frames = application::trackedFrames(*session, timebase, shown);
    if (!frames)
        return {};
    const QString clipboard = QGuiApplication::clipboard()->text();
    const auto pin = powerPin(clipboard);
    const bool fits = pin && static_cast<int>(pin->frames.size()) == frames->count;

    int relative = 1;
    if (const auto at = video.reportedFrame(); at && *at >= frames->first && *at < frames->first + frames->count)
        relative = *at - frames->first + 1;

    int firstRow = 0, lastRow = 0, row = 0;
    bool fitFlat = true;
    const auto &doc = session->document();
    persp::Reader reader;
    reader.document = &doc;
    const auto resolution = application::scriptResolution(doc);
    reader.width = resolution.width;
    reader.height = resolution.height;
    persp::StyleCache styles;
    for (const auto *line : doc.lines()) {
        ++row;
        if (!session->selection().selected.contains(line->id) || (shown && !shown(line->id)))
            continue;
        firstRow = firstRow ? firstRow : row;
        lastRow = row;
        if (session->selection().active == line->id) {
            const auto t = persp::readTags(reader, *line, styles).transform;
            fitFlat = t.frx == 0 && t.fry == 0;
        }
    }
    return {{QStringLiteral("data"), fits ? clipboard : QString()},
            {QStringLiteral("frames"), frames->count},
            {QStringLiteral("firstFrame"), frames->first},
            {QStringLiteral("relativeFrame"), relative},
            {QStringLiteral("lines"),
             firstRow == lastRow
                 ? QCoreApplication::translate("HikariSub.Tracking", "Line %1").arg(firstRow)
                 : QCoreApplication::translate("HikariSub.Tracking", "Lines %1–%2").arg(firstRow).arg(lastRow)},
            {QStringLiteral("fitFlat"), fitFlat}};
}

QVariantMap Application::checkPerspectiveTracking(const QString &data, int relativeFrame) const
{
    const auto result = [](const char *state, const QString &text, bool canApply = false) {
        return QVariantMap{{QStringLiteral("state"), QString::fromLatin1(state)},
                           {QStringLiteral("text"), text},
                           {QStringLiteral("canApply"), canApply}};
    };
    if (data.trimmed().isEmpty())
        return result("idle", QCoreApplication::translate("HikariSub.Tracking",
                                                          "Paste the Power-Pin data exported from After Effects or "
                                                          "Blender. Apply reads the four CC Power Pin corners."));
    auto *session = targetSession();
    if (!session)
        return {};
    const auto pin = powerPin(data);
    if (!pin)
        return result("error", QCoreApplication::translate(
                                   "HikariSub.Tracking", "This isn’t Power-Pin data: no CC Power Pin corners were found."));
    const auto &video = activeMedia().video->session();
    const auto check =
        application::checkTracking(*session, video.legacyTimebase(), *pin, relativeFrame, shownLines());
    const int count = static_cast<int>(pin->frames.size());
    const int first = check.frames.first, last = check.frames.first + check.frames.count - 1;
    if (check.problem) {
        switch (*check.problem) {
        case application::TrackingProblem::NoLines:
            return result("error", QCoreApplication::translate("HikariSub.Tracking", "Select the lines to track."));
        case application::TrackingProblem::Length:
            return result("error", QCoreApplication::translate("HikariSub.Tracking",
                                                               "The data has %n frame(s), but the selected lines cover "
                                                               "%1 (%2–%3). Select lines that span the tracked frames.",
                                                               nullptr, count)
                                       .arg(check.frames.count)
                                       .arg(first)
                                       .arg(last));
        case application::TrackingProblem::NoLineAtFrame:
            return result("error",
                          QCoreApplication::translate("HikariSub.Tracking", "No selected line is shown at frame %1.")
                              .arg(relativeFrame));
        case application::TrackingProblem::Overlap:
            return result("error", QCoreApplication::translate("HikariSub.Tracking",
                                                               "Lines overlap, but line %1 isn’t shown at frame %2. Pick "
                                                               "a frame every line shows, or apply them one at a time.")
                                       .arg(check.row)
                                       .arg(relativeFrame));
        }
    }
    const auto frame = video.lastFrame();
    if (pin->width > 0 && pin->height > 0 && frame && (pin->width != frame->width || pin->height != frame->height))
        return result("warning",
                      QCoreApplication::translate("HikariSub.Tracking", "Tracked at %1 × %2, but the video is %3 × %4. "
                                                                        "The corners are scaled to the video.")
                          .arg(pin->width)
                          .arg(pin->height)
                          .arg(frame->width)
                          .arg(frame->height),
                      true);
    if (pin->fps > 0 && pin->width > 0 && pin->height > 0)
        return result("ok",
                      QCoreApplication::translate("HikariSub.Tracking",
                                                  "%n frame(s) at %1 fps, tracked at %2 × %3. Covers the selection’s "
                                                  "frames %4–%5.",
                                                  nullptr, count)
                          .arg(QString::number(pin->fps, 'g', 6))
                          .arg(pin->width)
                          .arg(pin->height)
                          .arg(first)
                          .arg(last),
                      true);
    return result("ok",
                  QCoreApplication::translate("HikariSub.Tracking", "%n frame(s). Covers the selection’s frames %1–%2.",
                                              nullptr, count)
                      .arg(first)
                      .arg(last),
                  true);
}

QString Application::applyPerspectiveTracking(const QString &data, int relativeFrame, int orgMode, bool fitFlat,
                                              bool moveClips, bool scaleBorders)
{
    const auto pin = powerPin(data);
    const auto &video = activeMedia().video->session();
    if (!pin || video.state() != application::VideoSession::State::Ready)
        return QCoreApplication::translate("HikariSub.Tracking",
                                           "This isn’t Power-Pin data: no CC Power Pin corners were found.");
    application::TrackingOptions options;
    options.relativeFrame = relativeFrame;
    options.orgMode = static_cast<persp::OrgMode>(std::clamp(orgMode, 0, 2));
    options.fitFlat = fitFlat;
    options.moveClips = moveClips;
    options.scaleBorders = scaleBorders;
    if (const auto frame = video.lastFrame()) {
        options.videoWidth = frame->width;
        options.videoHeight = frame->height;
    }
    options.measure = perspectiveMeasure();
    const auto timebase = video.legacyTimebase();
    const auto shown = shownLines();
    const bool done = runFilter([&](application::EditSession &s) {
        return application::applyTracking(s, timebase, *pin, options, shown);
    });
    return done ? QString()
                : QCoreApplication::translate("HikariSub.Tracking",
                                              "A line can’t be tracked: its text can’t be measured or the plane folds it.");
}

}
