#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQuickItem;

namespace hikari::application {
class GeneralPlayerPort;
}

namespace hikari::ui {

class SettingsStore;
class VideoController;
class VisualToolsController;

class VideoViewController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool zoomMode READ zoomMode NOTIFY changed)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY changed)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY changed)
    Q_PROPERTY(QRectF zoomFrame READ zoomFrame NOTIFY changed)
    Q_PROPERTY(QRectF zoomBounds READ zoomBounds NOTIFY changed)
    // MUST KILL: zoomCursor is a magic int (0 arrow, 1 west-east, 2 north-south)
    Q_PROPERTY(int zoomCursor READ zoomCursor NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(bool progressBar READ progressBar NOTIFY changed)
    Q_PROPERTY(bool renderCost READ renderCost NOTIFY changed)
    Q_PROPERTY(bool canSnapshot READ canSnapshot NOTIFY changed)
    Q_PROPERTY(QString lastSnapshot READ lastSnapshot NOTIFY changed)
    Q_PROPERTY(bool pauseOnClick READ pauseOnClick NOTIFY changed)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY changed)
public:
    VideoViewController(VideoController &video, VisualToolsController &tools, SettingsStore &settings,
                        QObject *parent = nullptr);

    void setPlayer(application::GeneralPlayerPort *player);

    bool zoomMode() const;
    bool zoomed() const;
    int zoomPercent() const;
    QRectF zoomFrame() const;
    QRectF zoomBounds() const;
    int zoomCursor() const { return m_zoomCursor; }
    int volume() const;
    bool muted() const { return m_muted; }
    bool progressBar() const;
    bool renderCost() const;
    bool canSnapshot() const;
    QString lastSnapshot() const { return m_lastSnapshot; }
    bool pauseOnClick() const;

    Q_INVOKABLE bool toggleZoom();
    Q_INVOKABLE bool resetZoom();
    enum PointerRequest { NoRequest = 0, ContextMenuRequest = 1, FullScreenRequest = 2, MovedOverVideo = 3 };
    Q_ENUM(PointerRequest)
    // MUST KILL: pointer's kind is a magic int (6 a double click) and it returns a PointerRequest as int
    Q_INVOKABLE int pointer(int kind, qreal x, qreal y, int button, int buttons, int modifiers, int wheelSteps = 0);
    Q_INVOKABLE void panelWheel(int steps, int modifiers = 0);
    Q_INVOKABLE void volumeWheel(int steps);
    Q_INVOKABLE bool key(int key, int modifiers);
    bool fullscreen() const { return m_fullscreen; }
    void setFullscreen(bool on);

    Q_INVOKABLE bool stepVolume(bool up);
    // MUST KILL: setVolume relies on SettingsStore::changed to apply the volume
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void toggleMute();

    Q_INVOKABLE int aspectSliderValue() const;
    // MUST KILL: aspectLabel's negative sliderValue means the current ratio
    Q_INVOKABLE QString aspectLabel(int sliderValue) const;
    Q_INVOKABLE void setAspectFromSlider(int sliderValue);
    Q_INVOKABLE double aspectRatio() const;
    Q_INVOKABLE void setAspectRatio(double ratio);
    Q_INVOKABLE void resetAspectRatio();

    Q_INVOKABLE bool toggleProgressBar();
    Q_INVOKABLE bool toggleRenderCost();

    Q_INVOKABLE bool snapshot(const QString &action);

    Q_INVOKABLE QVariantMap recentFiles() const;
    Q_INVOKABLE QPointF cursorIn(QQuickItem *area) const;

signals:
    void changed();

private:
    void zoomPointer(int kind, int x, int y, int button, int buttons, int wheelSteps);
    void viewChanged();
    void applyVolume();

    VideoController &m_video;
    VisualToolsController &m_tools;
    SettingsStore &m_settings;
    application::GeneralPlayerPort *m_player = nullptr;
    int m_zoomCursor = 0;
    QString m_lastSnapshot;
    bool m_fullscreen = false;
    bool m_muted = false;
};

}
