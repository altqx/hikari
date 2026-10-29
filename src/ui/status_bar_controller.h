#pragma once

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
    Q_PROPERTY(QStringList fields READ fields NOTIFY changed)
    Q_PROPERTY(QStringList tooltips READ tooltips NOTIFY tooltipsChanged)
    Q_PROPERTY(bool resolutionMismatch READ resolutionMismatch NOTIFY changed)

public:
    enum Field {
        // MUST KILL: HelpText is always empty here; the shell's statusText fills field 0
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

    struct Inputs {
        bool video = false;
        int clientWidth = 0;
        int frameWidth = 0, frameHeight = 0;
        float zoomPercent = 1.f;
        std::int64_t durationMs = 0;
        float fps = 0.f;
        application::visual::AspectPair aspect;
        QString videoPath;
        bool document = false;
        const void *documentKey = nullptr;
        bool ass = false;
        application::Resolution script;
    };
    struct Fields {
        std::array<QString, FieldCount> text;
        bool mismatch = false;
    };
    static Fields fieldsFor(const Inputs &in);
    // MUST KILL: nextMismatch follows resolution-change events, not the state, so the colour can disagree with the current resolutions
    static bool nextMismatch(const Inputs &last, const Inputs &in, bool mismatch);

    using DocumentProvider = std::function<const core::Document *()>;
    StatusBarController(VideoController &video, VisualToolsController &tools, DocumentProvider document,
                        QObject *parent = nullptr);

    QStringList fields() const;
    static QStringList tooltips();
    bool resolutionMismatch() const { return m_fields.mismatch; }
    const Fields &current() const { return m_fields; }
    Inputs inputs() const;

    void refresh();
    void retranslate() { emit tooltipsChanged(); }

signals:
    void changed();
    void tooltipsChanged();

private:
    VideoController &m_video;
    VisualToolsController &m_tools;
    DocumentProvider m_document;
    Fields m_fields;
    Inputs m_last;
};

}
