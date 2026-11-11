#include "hikari/app/application.h"
#include "hikari/application/motion_data.h"
#include "hikari/application/perspective_tracking.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>

#include <algorithm>
#include <cmath>

namespace hikari::app {

namespace {

std::u8string utf8(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    return {reinterpret_cast<const char8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

QString qstring(std::u8string_view text)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size()));
}

std::optional<application::MotionTrack> motionTrack(const QString &text)
{
    return text.trimmed().isEmpty() ? std::nullopt : application::parseMotionData(utf8(text));
}

QVariantMap status(const char *state, const QString &text, bool canApply = false)
{
    return {{QStringLiteral("state"), QString::fromLatin1(state)},
            {QStringLiteral("text"), text},
            {QStringLiteral("canApply"), canApply}};
}

QVariantMap toast(bool ok, const QString &text)
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("text"), text}};
}

QString linesLabel(const application::EditSession &session, const application::LineVisible &shown)
{
    int firstRow = 0, lastRow = 0, row = 0;
    for (const auto *line : session.document().lines()) {
        ++row;
        if (!session.selection().selected.contains(line->id) || (shown && !shown(line->id)))
            continue;
        firstRow = firstRow ? firstRow : row;
        lastRow = row;
    }
    return firstRow == lastRow ? QCoreApplication::translate("HikariSub.Tracking", "Line %1").arg(firstRow) : QCoreApplication::translate("HikariSub.Tracking", "Lines %1–%2").arg(firstRow).arg(lastRow);
}

enum class Series { Position, Scale, Rotation };

QString seriesName(Series s)
{
    switch (s) {
    case Series::Position: return QCoreApplication::translate("HikariSub.Tracking", "Position");
    case Series::Scale: return QCoreApplication::translate("HikariSub.Tracking", "Scale");
    case Series::Rotation: return QCoreApplication::translate("HikariSub.Tracking", "Rotation");
    }
    return {};
}

struct Worst {
    Series series = Series::Position;
    const application::PolynomialFit *fit = nullptr;
};

Worst worstFit(const application::SeriesFits &fits)
{
    Worst out;
    for (std::size_t i = 0; i < fits.size(); ++i) {
        const Series series = i < 2 ? Series::Position : i < 4 ? Series::Scale : Series::Rotation;
        if (!out.fit || fits[i].confidence() > out.fit->confidence()) {
            out.fit = &fits[i];
            out.series = series;
        }
    }
    return out;
}

const application::PolynomialFit &positionFit(const application::SeriesFits &fits)
{
    return fits[0].rmse >= fits[1].rmse ? fits[0] : fits[1];
}

QString error(double rmse)
{
    return QString::number(rmse, 'g', 2);
}

QString problemText(application::ExtendFailure failure, int frames)
{
    using enum application::ExtendProblem;
    switch (failure.problem) {
    case TooFewLines: return QCoreApplication::translate("HikariSub.Tracking", "Select two or more lines, one a frame.");
    case NotFrameByFrame:
        return QCoreApplication::translate("HikariSub.Tracking", "Line %1 isn’t the next frame of the sign: each line must cover one frame and follow on from the "
                    "one before, with the same text and layer.")
            .arg(failure.row);
    case Animated:
        return QCoreApplication::translate("HikariSub.Tracking", "Line %1 is animated (\\t, \\move, \\fad or karaoke). Extend lines that hold still on each frame.")
            .arg(failure.row);
    case NoPosition: return QCoreApplication::translate("HikariSub.Tracking", "Line %1 has no \\pos.").arg(failure.row);
    case BeforeVideo: return QCoreApplication::translate("HikariSub.Tracking", "The video has fewer than %n frame(s) before line %1.", nullptr, frames).arg(failure.row);
    case Refused: break;
    }
    return QCoreApplication::translate("HikariSub.Tracking", "The lines can’t be extended.");
}

}

QVariantMap Application::motionDataValues() const
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    const auto times = video.aegisubFrameTimes();
    if (!session || video.state() != application::VideoSession::State::Ready || !times)
        return {};
    const auto shown = shownLines();
    const auto check = application::checkMotion(*session, *times, 0, shown);
    if (check.problem == application::MotionProblem::NoLines || check.frames <= 0)
        return {};
    const QString clipboard = QGuiApplication::clipboard()->text();
    int relative = 1;
    if (const auto at = video.reportedFrame(); at && *at >= check.firstFrame && *at < check.firstFrame + check.frames)
        relative = static_cast<int>(*at - check.firstFrame + 1);
    return {{QStringLiteral("data"), motionTrack(clipboard) ? clipboard : QString()},
            {QStringLiteral("frames"), check.frames},
            {QStringLiteral("firstFrame"), static_cast<qint64>(check.firstFrame)},
            {QStringLiteral("relativeFrame"), relative},
            {QStringLiteral("lines"), linesLabel(*session, shown)}};
}

QVariantMap Application::checkMotionData(const QString &data, const QString &clipData, int relativeFrame,
                                         int dataStart) const
{
    int dataFrames = -1;
    const auto finish = [&](QVariantMap main, const char *clipState = "idle", const QString &clipText = {}) {
        main.insert(QStringLiteral("clipState"), QString::fromLatin1(clipState));
        main.insert(QStringLiteral("clipText"), clipText);
        if (dataFrames >= 0)
            main.insert(QStringLiteral("dataFrames"), dataFrames);
        return main;
    };
    if (data.trimmed().isEmpty())
        return finish(status("idle", QCoreApplication::translate("HikariSub.Tracking", "Paste the After Effects keyframe data exported from Mocha or Blender. "
                    "Apply reads its Position, Scale and Rotation.")));
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    const auto times = video.aegisubFrameTimes();
    if (!session || !times)
        return {};
    const auto track = motionTrack(data);
    if (!track)
        return finish(status("error", QCoreApplication::translate("HikariSub.Tracking", "This isn’t motion data: no After Effects keyframe data with Position, "
                    "Scale and Rotation was found.")));
    dataFrames = track->frames();
    const auto check = application::checkMotion(*session, *times, track->frames(), shownLines());
    if (check.problem == application::MotionProblem::NoLines)
        return finish(status("error", QCoreApplication::translate("HikariSub.Tracking", "Select the lines to track.")));
    const int count = track->frames();
    const qint64 first = check.firstFrame, last = check.firstFrame + check.frames - 1;
    QVariantMap main;
    if (dataStart > 0 && count < check.frames) {
        const int before = dataStart - 1, after = check.frames - count - before;
        if (after < 0)
            return finish(status("error", QCoreApplication::translate("HikariSub.Tracking", "The data starts at frame %1 and runs past the selection’s last frame. "
                    "Pick an earlier start.")
                                              .arg(dataStart)));
        const auto extended = application::extendTrack(*track, before, after);
        QString text = QCoreApplication::translate("HikariSub.Tracking", "%n frame(s) of data for %1 frames: %2 extrapolated before, %3 after.", nullptr, count)
                           .arg(check.frames)
                           .arg(before)
                           .arg(after);
        const char *state = "ok";
        for (const auto &[fits, atStart] : {std::pair{extended.before, true}, std::pair{extended.after, false}}) {
            if (!fits || application::worstConfidence(*fits) == application::FitConfidence::Good)
                continue;
            const Worst worst = worstFit(*fits);
            const qint64 from = atStart ? first : last - after + 1, to = atStart ? first + before - 1 : last;
            text += QLatin1Char(' ') +
                    (atStart ? QCoreApplication::translate("HikariSub.Tracking", "%1 fits poorly at the start; check frames %2–%3.")
                             : QCoreApplication::translate("HikariSub.Tracking", "%1 fits poorly at the end; check frames %2–%3."))
                        .arg(seriesName(worst.series))
                        .arg(from)
                        .arg(to);
            state = "warning";
            break;
        }
        main = status(state, text, true);
    } else if (check.problem == application::MotionProblem::Length) {
        return finish(status("error", QCoreApplication::translate("HikariSub.Tracking", "The data has %n frame(s), but the selected lines cover %1 (%2–%3). Select "
                    "lines that span the tracked frames, or extrapolate.", nullptr, count)
                                          .arg(check.frames)
                                          .arg(first)
                                          .arg(last)));
    } else if (track->fps > 0) {
        main = status("ok",
                      QCoreApplication::translate("HikariSub.Tracking", "%n frame(s) at %1 fps, tracked at %2 × %3. Covers the selection’s frames %4–%5.", nullptr, count)
                          .arg(QString::number(track->fps, 'g', 6))
                          .arg(track->width)
                          .arg(track->height)
                          .arg(first)
                          .arg(last),
                      true);
    } else {
        main = status("ok", QCoreApplication::translate("HikariSub.Tracking", "%n frame(s). Covers the selection’s frames %1–%2.", nullptr, count).arg(first).arg(last), true);
    }
    if (clipData.trimmed().isEmpty())
        return finish(main, "idle", QCoreApplication::translate("HikariSub.Tracking", "Paste the data the clips follow."));
    const auto clip = motionTrack(clipData);
    if (!clip) {
        main.insert(QStringLiteral("canApply"), false);
        return finish(main, "error", QCoreApplication::translate("HikariSub.Tracking", "This isn’t motion data."));
    }
    if (clip->frames() != count) {
        main.insert(QStringLiteral("canApply"), false);
        return finish(main, "error", QCoreApplication::translate("HikariSub.Tracking", "The clip data has %n frame(s); the motion data has %1.", nullptr, clip->frames()).arg(count));
    }
    return finish(main, "ok", QCoreApplication::translate("HikariSub.Tracking", "%n frame(s). Moves the clips from frame %1.", nullptr, count).arg(relativeFrame));
}

QString Application::applyMotionData(const QString &data, const QString &clipData, const QVariantMap &o)
{
    const auto &video = activeMedia().video->session();
    const auto times = video.aegisubFrameTimes();
    auto track = motionTrack(data);
    if (!track || !times || video.state() != application::VideoSession::State::Ready)
        return QCoreApplication::translate("HikariSub.Tracking", "This isn’t motion data: no After Effects keyframe data with Position, Scale and Rotation was found.");
    const auto flag = [&](const char *name, bool fallback) {
        const QVariant v = o.value(QLatin1String(name));
        return v.isValid() ? v.toBool() : fallback;
    };
    application::MotionOptions options;
    application::MotionFollow follow;
    follow.relativeFrame = std::max(1, o.value(QStringLiteral("relativeFrame"), 1).toInt());
    follow.x = flag("x", true);
    follow.y = flag("y", true);
    follow.scale = flag("scale", true);
    follow.rotation = flag("rotation", false);
    follow.rectClips = flag("rectClips", true);
    follow.vectorClips = flag("vectorClips", true);
    follow.rectToVector = flag("rectToVector", false);
    options.main = follow;
    options.absolute = flag("absolute", false);
    options.origin = flag("origin", false);
    options.border = flag("border", true);
    options.shadow = flag("shadow", true);
    options.blur = flag("blur", true);
    options.blurScale = o.value(QStringLiteral("blurPercent"), 100).toDouble() / 100;
    options.clipsOnly = flag("clipsOnly", false);
    options.bakeTransforms = flag("bake", true);
    options.linear = flag("linear", false);
    const bool withClips = flag("clipData", false);
    std::optional<application::MotionTrack> clip;
    if (withClips) {
        clip = motionTrack(clipData);
        if (!clip)
            return QCoreApplication::translate("HikariSub.Tracking", "This isn’t motion data.");
    }
    const auto shown = shownLines();
    if (const int start = o.value(QStringLiteral("dataStart"), 0).toInt(); start > 0) {
        auto *session = targetSession();
        if (!session)
            return QCoreApplication::translate("HikariSub.Tracking", "Select the lines to track.");
        const auto check = application::checkMotion(*session, *times, 0, shown);
        const int before = start - 1, after = check.frames - track->frames() - before;
        if (track->frames() < check.frames && after >= 0) {
            track = application::extendTrack(*track, before, after).track;
            if (clip)
                clip = application::extendTrack(*clip, before, after).track;
        }
    }
    if (clip) {
        options.clipTrack = *clip;
        options.clip = follow;
    }
    const bool done = runFilter([&](application::EditSession &s) {
        return application::applyMotion(s, *times, *track, options, shown);
    });
    return done ? QString() : QCoreApplication::translate("HikariSub.Tracking", "The data doesn’t cover the selected lines’ frames.");
}

QString Application::revertMotionData()
{
    const bool done = runFilter([&](application::EditSession &s) { return application::revertMotion(s, shownLines()); });
    return done ? QString() : QCoreApplication::translate("HikariSub.Tracking", "No selected line has motion data to revert.");
}

QVariantMap Application::copyMotionData()
{
    auto *session = targetSession();
    const auto times = activeMedia().video->session().aegisubFrameTimes();
    if (!session || !times)
        return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Open a video first: motion data is per frame."));
    const auto track = application::deriveMotion(*session, *times, shownLines());
    if (!track) {
        using enum application::DeriveProblem;
        switch (track.error().problem) {
        case TooFewLines: return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Select two or more lines of one sign, frame by frame."));
        case NotFrameByFrame:
            return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Line %1 doesn’t follow on from the line before with the same text and layer.")
                                    .arg(track.error().row));
        case NoPosition: return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Line %1 has no \\pos or \\move.").arg(track.error().row));
        }
    }
    QGuiApplication::clipboard()->setText(qstring(application::writeMotionData(*track)));
    return toast(true, QCoreApplication::translate("HikariSub.Tracking", "Copied %n frame(s) of motion data", nullptr, track->frames()));
}

QVariantMap Application::copyPowerPinTrack()
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    const auto times = video.aegisubFrameTimes();
    if (!session || video.state() != application::VideoSession::State::Ready || !times)
        return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Open a video first: tracking data is per frame."));
    const auto frame = video.lastFrame();
    const auto text = application::derivePowerPinTrack(*session, video.legacyTimebase(), times->fps(),
                                                       frame ? frame->height : 0, perspectiveMeasure(), shownLines());
    if (!text) {
        using enum application::PowerPinProblem;
        const int row = text.error().row;
        switch (text.error().problem) {
        case NoLines: return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Select the lines to copy."));
        case Overlap: return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Line %1 overlaps another selected line. Copy one sign at a time.").arg(row));
        case Move: return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Line %1 has \\move, which a Power-Pin track can’t hold.").arg(row));
        case Unmeasured: return toast(false, QCoreApplication::translate("HikariSub.Tracking", "Line %1’s text can’t be measured.").arg(row));
        }
    }
    const auto pin = application::parsePowerPin(*text);
    QGuiApplication::clipboard()->setText(qstring(*text));
    return toast(true, QCoreApplication::translate("HikariSub.Tracking", "Copied %n frame(s) of Power-Pin data", nullptr, pin ? static_cast<int>(pin->frames.size()) : 0));
}

QVariantMap Application::extrapolateMoves()
{
    int changed = 0;
    const bool done = runFilter([&](application::EditSession &s) -> std::expected<void, application::CommandRefusal> {
        const auto n = application::extrapolateMoves(s, shownLines());
        if (!n)
            return std::unexpected(n.error());
        changed = *n;
        return {};
    });
    return done ? toast(true, QCoreApplication::translate("HikariSub.Tracking", "Extrapolated \\move on %n line(s)", nullptr, changed))
                : toast(false, QCoreApplication::translate("HikariSub.Tracking", "No selected line has a \\move with start and end times."));
}

QVariantMap Application::extendTrackingValues() const
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    const auto times = video.aegisubFrameTimes();
    if (!session || video.state() != application::VideoSession::State::Ready || !times)
        return {};
    const auto shown = shownLines();
    const auto check = application::checkMotion(*session, *times, 0, shown);
    if (check.problem == application::MotionProblem::NoLines || check.frames <= 0)
        return {};
    return {{QStringLiteral("frames"), check.frames},
            {QStringLiteral("firstFrame"), static_cast<qint64>(check.firstFrame)},
            {QStringLiteral("lines"), linesLabel(*session, shown)}};
}

QVariantMap Application::checkExtendTracking(int frames, bool after) const
{
    auto *session = targetSession();
    const auto times = activeMedia().video->session().aegisubFrameTimes();
    if (!session || !times)
        return {};
    const auto shown = shownLines();
    const auto fits =
        application::checkExtend(*session, *times, frames, after ? application::ExtendAt::End : application::ExtendAt::Start, shown);
    if (!fits)
        return status("error", problemText(fits.error(), frames));
    int lines = 0;
    for (const auto *line : session->document().lines())
        lines += session->selection().selected.contains(line->id) && (!shown || shown(line->id)) && !line->comment;
    const auto &position = positionFit(*fits);
    QString text = QCoreApplication::translate("HikariSub.Tracking", "Fits position, scale and rotation over the %n selected line(s) (degree %1, error %2 px).", nullptr, lines)
                       .arg(position.degree())
                       .arg(error(position.rmse));
    if (application::worstConfidence(*fits) == application::FitConfidence::Good)
        return status("ok", text, true);
    const Worst worst = worstFit(*fits);
    text += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.Tracking", "%1 fits poorly (error %2); check the new frames.")
                                   .arg(seriesName(worst.series))
                                   .arg(error(worst.fit->rmse));
    return status("warning", text, true);
}

QString Application::extendTrackedLines(int frames, bool after, bool origin)
{
    const auto times = activeMedia().video->session().aegisubFrameTimes();
    if (!times)
        return QCoreApplication::translate("HikariSub.Tracking", "Open a video first: tracking data is per frame.");
    std::optional<application::ExtendFailure> failure;
    const bool done = runFilter([&](application::EditSession &s) -> std::expected<void, application::CommandRefusal> {
        const auto fits = application::extendTrackedLines(
            s, *times, frames, after ? application::ExtendAt::End : application::ExtendAt::Start, origin, shownLines());
        if (fits)
            return {};
        failure = fits.error();
        return std::unexpected(application::CommandRefusal::Invalid);
    });
    return done ? QString() : problemText(failure.value_or(application::ExtendFailure{application::ExtendProblem::Refused}), frames);
}

QVariantMap Application::readTrackingFile(const QUrl &url) const
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly))
        return {{QStringLiteral("error"), file.errorString()}};
    constexpr qint64 kLimit = 16 * 1024 * 1024;
    if (file.size() > kLimit)
        return {{QStringLiteral("error"), QCoreApplication::translate("HikariSub.Tracking", "The file is too large to be motion data.")}};
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return {{QStringLiteral("text"), text}};
}

}
