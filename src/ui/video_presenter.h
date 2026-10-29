#pragma once

#include "hikari/application/presenter.h"

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <QRect>
#include <QSize>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace hikari::ui {

class VideoPresenter : public QQuickItem, public application::PresenterPort {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(quint64 presentedGeneration READ presentedGeneration NOTIFY presented)
    Q_PROPERTY(QRectF videoRect READ videoRect WRITE setVideoRect NOTIFY placementChanged)
    Q_PROPERTY(QRectF sourceRect READ sourceRect WRITE setSourceRect NOTIFY placementChanged)
    Q_PROPERTY(QVariantMap renderCost READ renderCost NOTIFY renderCostChanged)
    Q_PROPERTY(QSize screenSize READ screenSize NOTIFY screenSizeChanged)
public:
    explicit VideoPresenter(QQuickItem *parent = nullptr);
    ~VideoPresenter() override;

    void present(application::Presentation presentation, Done done) override;
    bool convertsYcbcr() const override { return m_convertsYcbcr.load(); }

    quint64 presentedGeneration() const { return m_presentedGeneration; }
    QVariantMap renderCost() const { return m_renderCost; }
    QRectF frameRect() const;
    QSize screenSize() const { return m_screenSize; }
    QRectF videoRect() const { return m_videoRect; }
    void setVideoRect(const QRectF &rect);
    QRectF sourceRect() const { return m_sourceRect; }
    void setSourceRect(const QRectF &rect);
    std::uint64_t rejectedSubmissions() const { return m_rejected; }
    struct UploadStats {
        int frameUploads = 0;
        int overlayUploads = 0;
        QRect lastOverlayUpload;
        int planarFrameUploads = 0;
        int cpuConvertedFrames = 0;
        std::size_t lastFrameBytes = 0;
    };
    UploadStats uploadStats() const;

signals:
    void presented();
    void renderCostChanged();
    void placementChanged();
    void screenSizeChanged();

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
    void onFrameSwapped();
    QRectF placement(QSizeF frame, double pixelAspect) const;
    void updateScreenSize();

    std::optional<Submission> m_pending;
    application::Presentation m_shown;
    std::atomic<std::uint64_t> m_surfaceEpoch{1};
    std::atomic<bool> m_convertsYcbcr{false};
    std::mutex m_renderMutex;
    std::optional<Resolved> m_awaitingRender;
    QMetaObject::Connection m_swapped, m_invalidated;
    QQuickWindow *m_attached = nullptr;
    quint64 m_presentedGeneration = 0;
    QVariantMap m_renderCost;
    std::uint64_t m_rejected = 0;
    QRectF m_videoRect, m_sourceRect;
    QSize m_frameSize;
    double m_pixelAspect = 1;
    QSize m_screenSize;
    struct Stats : UploadStats {
        std::mutex mutex;
        bool failed = false;
    };
    std::shared_ptr<Stats> m_stats = std::make_shared<Stats>();
};

}
