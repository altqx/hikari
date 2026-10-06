#pragma once

// P10 (#200): legacy's status bar, HikariStatusBar with the nine fields
// HikariSubFrame sets up (HikariSubFrame.cpp:150-155):
//
//   0 help and progress text (the shell's statusText: menu help, tooltips,
//     Automation's set_status_text, "Autosave", progress);
//   1 video scale, 2 video zoom (VideoBox::SetScaleAndZoom, VideoBox.cpp:1301-1315);
//   3 video duration, 4 frames per second, 6 video aspect ratio, 8 video file
//     name (VideoBox::OpenVideo, VideoBox.cpp:395-404, and OnPageChanged,
//     HikariSubFrame.cpp:1940-1974);
//   5 video resolution (SetVideoResolution, HikariSubFrame.cpp:1475-1497);
//   7 subtitles resolution (SetSubsResolution, HikariSubFrame.cpp:1439-1473),
//     fields 5 and 7 in WINDOW_WARNING_ELEMENTS while the two differ.
//
// Legacy set each field when its source changed; here every field is derived
// from the current state, which is what legacy's setters leave after an
// open, a tab change, a resize, a zoom or a resolution change. An empty field
// takes no space (HikariStatusBar::CalcWidths gives an empty auto-sized field
// width 0). Legacy's main toolbar is not carried over (user decision
// 2026-10-05): the menus and shortcuts carry its actions.

#include "hikari/application/resample.h"
#include "hikari/application/visual_view.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace hikari::core {
class Document;
}

namespace hikari::ui {

class VideoController;
class VisualToolsController;

class StatusBarController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // The nine fields' texts; field 0 is the shell's statusText, so it is
    // always empty here.
    Q_PROPERTY(QStringList fields READ fields NOTIFY changed)
    // Legacy's tooltips (HikariSubFrame.cpp:153-154), field 0 has none.
    Q_PROPERTY(QStringList tooltips READ tooltips CONSTANT)
    // Fields 5 and 7 in the warning colour: the subtitles' resolution is not
    // the video's (legacy badResolution).
    Q_PROPERTY(bool resolutionMismatch READ resolutionMismatch NOTIFY changed)

public:
    enum Field {
        HelpText = 0,
        VideoScale,
        VideoZoom,
        VideoDuration,
        FramesPerSecond,
        VideoResolution,
        AspectRatio,
        SubtitlesResolution,
        VideoName,
        FieldCount
    };

    // What the fields show, read from the shell's state.
    struct Inputs {
        bool video = false; // the video is open (legacy GetState() != None)
        // The Video panel's view (legacy VideoBox and RendererVideo): its
        // width in device pixels, the frame size (m_Width, m_Height) and
        // m_ZoomPercent; frameWidth 0 while the view has no video.
        int clientWidth = 0;
        int frameWidth = 0, frameHeight = 0;
        float zoomPercent = 1.f;
        std::int64_t durationMs = 0; // GetDuration(): the last frame's time
        float fps = 0.f;             // m_FPS
        application::visual::AspectPair aspect; // m_AspectRatioX, m_AspectRatioY
        QString videoPath;
        // The editing target, if any: ASS, and its script resolution (GetASSRes).
        bool document = false;
        const void *documentKey = nullptr; // which Document: another one is a SetSubsResolution
        bool ass = false;
        application::Resolution script;
    };
    struct Fields {
        std::array<QString, FieldCount> text;
        bool mismatch = false;
    };
    // The texts (the colour is not part of the state alone).
    static Fields fieldsFor(const Inputs &in);
    // Fields 5 and 7's colour after `in`, from `last` and the colour then.
    static bool nextMismatch(const Inputs &last, const Inputs &in, bool mismatch);

    using DocumentProvider = std::function<const core::Document *()>;
    StatusBarController(VideoController &video, VisualToolsController &tools, DocumentProvider document,
                        QObject *parent = nullptr);

    QStringList fields() const;
    static QStringList tooltips();
    bool resolutionMismatch() const { return m_fields.mismatch; }
    const Fields &current() const { return m_fields; }
    Inputs inputs() const;

    // The editing target or its content changed (the shell's refresh).
    void refresh();

signals:
    void changed();

private:
    VideoController &m_video;
    VisualToolsController &m_tools;
    DocumentProvider m_document;
    Fields m_fields;
    Inputs m_last;
};

} // namespace hikari::ui
