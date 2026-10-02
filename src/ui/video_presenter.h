#pragma once

// The scene-graph presenter (N7): a frame layer and a subtitle overlay layer.
// present() only stores the leases and schedules a sync; textures are made
// from the leased bytes on the render thread, and the node keeps the leases
// alive while its textures use them. A newer submission supersedes an older
// one that has not been shown yet. Moving to another window or losing the
// scene graph starts a new surface epoch: submissions made for the old
// surface are rejected instead of uploaded, while the content already shown
// is uploaded again for the new surface.

#include "hikari/application/presenter.h"

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

namespace hikari::ui {

class VideoPresenter : public QQuickItem, public application::PresenterPort {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(quint64 presentedGeneration READ presentedGeneration NOTIFY presented)
public:
    explicit VideoPresenter(QQuickItem *parent = nullptr);
    ~VideoPresenter() override;

    void present(application::Presentation presentation, Done done) override;

    quint64 presentedGeneration() const { return m_presentedGeneration; }
    // Where the frame is drawn, in item coordinates (empty with no frame).
    QRectF frameRect() const;
    std::uint64_t rejectedSubmissions() const { return m_rejected; }

signals:
    void presented();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct Submission {
        application::Presentation presentation;
        Done done;
        std::uint64_t surfaceEpoch = 0;
    };
    struct Resolved {
        Done done;
        application::PresentResult result;
    };

    void attachWindow(QQuickWindow *window);
    void resolveLater(Done done, application::PresentResult result);
    void onFrameSwapped(); // render thread

    std::optional<Submission> m_pending;      // touched on the GUI thread and during sync
    application::Presentation m_shown;        // what the node shows (or will after upload)
    std::atomic<std::uint64_t> m_surfaceEpoch{1};
    std::mutex m_renderMutex;
    std::optional<Resolved> m_awaitingRender; // uploaded, waiting for its frame to be swapped
    QMetaObject::Connection m_swapped, m_invalidated;
    QQuickWindow *m_attached = nullptr; // the window whose signals are connected
    quint64 m_presentedGeneration = 0;
    std::uint64_t m_rejected = 0;
};

} // namespace hikari::ui
