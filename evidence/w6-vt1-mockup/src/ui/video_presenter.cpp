#include "video_presenter.h"

#include "ycbcr_node.h"

#include <QImage>
#include <QPointer>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <rhi/qrhi.h>

#include "hikari/application/playback_pipeline.h"
#include "hikari/application/ycbcr_frame.h"

namespace hikari::ui {

using application::PresentOutcome;
using application::PresentResult;
using application::PresentStage;

namespace {

QRect rectOf(const std::optional<application::PixelRect> &r)
{
    return r ? QRect(r->x, r->y, r->width, r->height) : QRect();
}

// A texture kept across frames and updated in place on the render thread
// (RHI backends): the frame's image whole, the overlay's only where it or
// the overlay before it has visible pixels. The image given stays valid
// until the next commit (the node keeps the lease it points into). A
// texture that cannot be made (larger than the backend allows, or refused)
// is reported through `failed`; the image stays pending and the next sync
// gives it again.
class UpdatableTexture : public QSGTexture {
public:
    using Uploaded = std::function<void(const QRect &)>;
    using Failed = std::function<void()>;
    UpdatableTexture(bool alpha, Uploaded uploaded, Failed failed)
        : m_alpha(alpha), m_uploaded(std::move(uploaded)), m_failed(std::move(failed))
    {
    }
    ~UpdatableTexture() override
    {
        if (m_texture)
            m_texture->deleteLater();
    }
    qint64 comparisonKey() const override { return static_cast<qint64>(reinterpret_cast<quintptr>(this)); }
    QRhiTexture *rhiTexture() const override { return m_texture; }
    QSize textureSize() const override { return m_size; }
    bool hasAlphaChannel() const override { return m_alpha; }
    bool hasMipmaps() const override { return false; }
    // The last commit could not make the texture (render thread, also
    // where the node syncs).
    bool failed() const { return m_failure; }

    // The whole image next commit; `lease` keeps its pixels.
    void setFrame(const QImage &image, std::shared_ptr<const void> lease)
    {
        m_image = image;
        m_frame = std::move(lease);
        m_size = image.size();
        m_overlay.reset();
        m_bounds.reset();
        m_pending = true;
    }
    // An overlay: what changes is where it or the previous one has visible
    // pixels (`bounds` when known; else found at commit, off the GUI thread).
    void setOverlay(const QImage &image, std::shared_ptr<const application::OverlayFrame> lease,
                    std::optional<QRect> bounds)
    {
        m_image = image;
        m_size = image.size();
        m_overlay = std::move(lease);
        m_bounds = bounds;
        m_pending = true;
    }

    void commitTextureOperations(QRhi *rhi, QRhiResourceUpdateBatch *updates) override
    {
        if (!m_pending)
            return;
        bool whole = false;
        if (!m_texture || m_texture->pixelSize() != m_size) {
            if (m_texture)
                m_texture->deleteLater();
            m_texture = nullptr;
            const int largest = rhi->resourceLimit(QRhi::TextureSizeMax);
            QRhiTexture *texture = nullptr;
            if (m_size.width() <= largest && m_size.height() <= largest) {
                texture = rhi->newTexture(QRhiTexture::BGRA8, m_size);
                if (!texture->create()) {
                    delete texture;
                    texture = nullptr;
                }
            }
            if (!texture) {
                m_failure = true;
                if (m_failed)
                    m_failed();
                return;
            }
            m_texture = texture;
            whole = true;
            m_content = QRect();
        }
        m_pending = false;
        m_failure = false;
        QRect region(QPoint(0, 0), m_size);
        if (m_overlay) {
            const QRect visible = m_bounds ? *m_bounds : rectOf(application::overlayBounds(*m_overlay));
            if (!whole)
                region = visible.united(m_content); // clears what the last one drew
            m_content = visible;
        }
        if (m_uploaded)
            m_uploaded(region);
        if (!region.isEmpty()) {
            QRhiTextureSubresourceUploadDescription part(m_image);
            part.setSourceTopLeft(region.topLeft());
            part.setSourceSize(region.size());
            part.setDestinationTopLeft(region.topLeft());
            updates->uploadTexture(m_texture, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, part)}));
        }
        m_image = QImage();
        m_overlay.reset();
        m_frame.reset();
    }

private:
    bool m_alpha;
    Uploaded m_uploaded;
    Failed m_failed;
    bool m_failure = false;
    std::shared_ptr<const void> m_frame;
    QRhiTexture *m_texture = nullptr;
    QSize m_size;
    QImage m_image;
    std::shared_ptr<const application::OverlayFrame> m_overlay;
    std::optional<QRect> m_bounds;
    QRect m_content; // the overlay texels that may be visible
    bool m_pending = false;
};

// Holds the leases its textures were made from.
class PresentNode : public QSGNode {
public:
    QSGRectangleNode *background = nullptr;
    // The frame layer: a BGRA texture, or Y'CbCr planes converted on the GPU
    // (one of the two in the tree, after the background).
    QSGSimpleTextureNode *frame = nullptr;
    YcbcrNode *ycbcr = nullptr;
    QSGSimpleTextureNode *overlay = nullptr; // in the tree only while it draws something
    std::shared_ptr<const application::IndexedFrame> frameLease;
    // The BGRA the frame texture shows: the frame's own, or its Y'CbCr
    // planes converted on the CPU.
    std::shared_ptr<const application::IndexedFrame> bgraLease;
    std::shared_ptr<const application::OverlayFrame> overlayLease; // null: no overlay drawn
    QRect overlayRegion; // the frame pixels the overlay texture holds
    bool frameAttached = true;
    bool ycbcrAttached = false;
    bool overlayAttached = false;
    ~PresentNode() override
    {
        if (!frameAttached)
            delete frame;
        if (!ycbcrAttached)
            delete ycbcr;
        if (!overlayAttached)
            delete overlay;
    }
    // The frame layer shows the GPU-converted planes, or the BGRA texture.
    void showPlanes(bool planes)
    {
        QSGNode *show = planes ? static_cast<QSGNode *>(ycbcr) : frame;
        QSGNode *hide = planes ? static_cast<QSGNode *>(frame) : ycbcr;
        bool &shown = planes ? ycbcrAttached : frameAttached;
        bool &hidden = planes ? frameAttached : ycbcrAttached;
        if (hidden) {
            removeChildNode(hide);
            hidden = false;
        }
        if (!shown) {
            insertChildNodeAfter(show, background);
            shown = true;
        }
    }
};

// Visible pixels plus a margin of transparent ones, so a filtered edge
// samples as it would in the whole overlay.
QRect padded(const QRect &bounds, const QSize &size)
{
    return bounds.adjusted(-2, -2, 2, 2).intersected(QRect(QPoint(0, 0), size));
}

QString layoutError(const application::Presentation &p)
{
    const auto &f = p.frame;
    if (!f)
        return QStringLiteral("no frame");
    if (f->ycbcr) {
        if (!application::ycbcrLayoutError(*f).empty())
            return QStringLiteral("frame layout does not match its buffer");
    } else if (f->width <= 0 || f->height <= 0 || f->stride < f->width * 4 ||
               f->bgra.size() < static_cast<std::size_t>(f->stride) * static_cast<std::size_t>(f->height)) {
        return QStringLiteral("frame layout does not match its buffer");
    }
    if (p.overlay && !p.overlay->empty) {
        const auto &o = *p.overlay;
        if (o.width != f->width || o.height != f->height || o.stride < o.width * 4 ||
            o.pixels.size() < static_cast<std::size_t>(o.stride) * static_cast<std::size_t>(o.height))
            return QStringLiteral("overlay layout does not match the frame");
    }
    if (!(p.transform.pixelAspect > 0))
        return QStringLiteral("pixel aspect must be positive");
    return {};
}

} // namespace

VideoPresenter::VideoPresenter(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    // An item constructed with a parent joins its window before this
    // constructor runs, so itemChange() never saw that scene change.
    attachWindow(window());
}

VideoPresenter::~VideoPresenter()
{
    if (m_pending && m_pending->done)
        m_pending->done({m_pending->presentation.generation, PresentOutcome::Error, PresentStage::Submitted,
                         "the presenter was destroyed"});
    std::lock_guard lock(m_renderMutex);
    if (m_awaitingRender && m_awaitingRender->done)
        m_awaitingRender->done({m_awaitingRender->result.generation, PresentOutcome::Error, PresentStage::Uploaded,
                                "the presenter was destroyed"});
}

void VideoPresenter::present(application::Presentation presentation, Done done)
{
    if (const QString error = layoutError(presentation); !error.isEmpty())
        return resolveLater(std::move(done), {presentation.generation, PresentOutcome::Error,
                                              PresentStage::Submitted, error.toStdString()});
    if (m_pending)
        resolveLater(std::move(m_pending->done), {m_pending->presentation.generation, PresentOutcome::Superseded,
                                                  PresentStage::Submitted, {}});
    if (window() != m_attached)
        attachWindow(window());
    m_pending = Submission{std::move(presentation), std::move(done), m_surfaceEpoch.load()};
    update();
}

void VideoPresenter::resolveLater(Done done, PresentResult result)
{
    if (!done)
        return;
    QMetaObject::invokeMethod(
        this, [done = std::move(done), result = std::move(result)] { done(result); }, Qt::QueuedConnection);
}

void VideoPresenter::setVideoRect(const QRectF &rect)
{
    if (rect == m_videoRect)
        return;
    m_videoRect = rect;
    emit placementChanged();
    update(); // placement only
}

void VideoPresenter::setSourceRect(const QRectF &rect)
{
    if (rect == m_sourceRect)
        return;
    m_sourceRect = rect;
    emit placementChanged();
    update();
}

QRectF VideoPresenter::frameRect() const
{
    if (!m_shown.frame)
        return {};
    if (!m_videoRect.isEmpty())
        return m_videoRect;
    const double displayWidth = m_shown.frame->width * m_shown.transform.pixelAspect;
    const double displayHeight = m_shown.frame->height;
    const double scale = std::min(width() / displayWidth, height() / displayHeight);
    const QSizeF size(displayWidth * scale, displayHeight * scale);
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

void VideoPresenter::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange && value.window != m_attached) {
        ++m_surfaceEpoch;
        attachWindow(value.window);
    }
    QQuickItem::itemChange(change, value);
}

void VideoPresenter::attachWindow(QQuickWindow *window)
{
    disconnect(m_swapped);
    disconnect(m_invalidated);
    m_attached = window;
    if (!window)
        return;
    m_swapped = connect(window, &QQuickWindow::frameSwapped, this, &VideoPresenter::onFrameSwapped,
                        Qt::DirectConnection);
    m_invalidated = connect(window, &QQuickWindow::sceneGraphInvalidated, this, [this] { ++m_surfaceEpoch; },
                            Qt::DirectConnection);
}

void VideoPresenter::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update(); // placement only; nothing is uploaded again
}

QSGNode *VideoPresenter::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<PresentNode *>(oldNode);
    QQuickWindow *win = window();
    bool upload = !node && m_shown.frame; // a new surface: show the current content again
    std::optional<Resolved> uploaded;

    if (m_pending) {
        Submission s = std::move(*m_pending);
        m_pending.reset();
        if (s.surfaceEpoch != m_surfaceEpoch.load()) {
            ++m_rejected;
            resolveLater(std::move(s.done), {s.presentation.generation, PresentOutcome::Error,
                                             PresentStage::Submitted, "the surface changed before upload"});
        } else {
            m_shown = std::move(s.presentation);
            upload = true;
            uploaded = Resolved{std::move(s.done), {m_shown.generation, PresentOutcome::Accepted,
                                                    PresentStage::Uploaded, {}}};
        }
    }
    if (!m_shown.frame) {
        delete node;
        return nullptr;
    }
    // V1: the RHI backends keep their textures and update them in place; the
    // software renderer makes images.
    QRhi *rhi = win->rhi();
    const bool inPlace = rhi && rhi->isTextureFormatSupported(QRhiTexture::BGRA8);
    const bool planes = ycbcrSupported(rhi);
    m_convertsYcbcr = planes;
    if (!node) {
        node = new PresentNode;
        node->background = win->createRectangleNode();
        node->background->setColor(Qt::black);
        node->appendChildNode(node->background);
        node->frame = new QSGSimpleTextureNode;
        node->frame->setOwnsTexture(true);
        node->appendChildNode(node->frame);
        node->overlay = new QSGSimpleTextureNode;
        node->overlay->setOwnsTexture(true);
        upload = true;
    }
    if (upload) {
        // The same frame again (new subtitles over it): its texture stays.
        auto *kept = dynamic_cast<UpdatableTexture *>(node->frame->texture());
        const bool gpuPlanes = m_shown.frame->ycbcr && planes;
        const bool same = node->frameLease == m_shown.frame &&
                          (gpuPlanes ? node->ycbcrAttached && !node->ycbcr->failed()
                                     : node->frameAttached && node->frame->texture() && !(kept && kept->failed()));
        if (!same && gpuPlanes) {
            if (!node->ycbcr)
                node->ycbcr = new YcbcrNode(
                    [stats = m_stats](std::size_t bytes) {
                        std::lock_guard lock(stats->mutex);
                        ++stats->frameUploads;
                        ++stats->planarFrameUploads;
                        stats->lastFrameBytes = bytes;
                    },
                    [stats = m_stats] {
                        std::lock_guard lock(stats->mutex);
                        stats->failed = true;
                    });
            node->ycbcr->setFrame(m_shown.frame);
            node->showPlanes(true);
            node->frameLease = m_shown.frame;
            node->bgraLease = nullptr;
        } else if (!same) {
            node->showPlanes(false);
            std::shared_ptr<const application::IndexedFrame> bgra = m_shown.frame;
            if (bgra->ycbcr) { // no GPU conversion here: the CPU's, to the same pixels
                auto converted = std::make_shared<application::IndexedFrame>();
                if (!application::ycbcrToBgra(*bgra, *converted)) {
                    if (uploaded)
                        resolveLater(std::move(uploaded->done), {m_shown.generation, PresentOutcome::Error,
                                                                 PresentStage::Submitted, "texture upload failed"});
                    return node;
                }
                bgra = std::move(converted);
                std::lock_guard lock(m_stats->mutex);
                ++m_stats->cpuConvertedFrames;
            }
            const auto &f = *bgra;
            const QImage frameImage(reinterpret_cast<const uchar *>(f.bgra.data()), f.width, f.height, f.stride,
                                    QImage::Format_RGB32);
            if (inPlace) {
                auto *texture = dynamic_cast<UpdatableTexture *>(node->frame->texture());
                if (!texture) {
                    texture = new UpdatableTexture(
                        false,
                        [stats = m_stats](const QRect &region) {
                            std::lock_guard lock(stats->mutex);
                            ++stats->frameUploads;
                            stats->lastFrameBytes = static_cast<std::size_t>(region.width()) * region.height() * 4;
                        },
                        [stats = m_stats] {
                            std::lock_guard lock(stats->mutex);
                            stats->failed = true;
                        });
                    texture->setFiltering(QSGTexture::Linear);
                    node->frame->setTexture(texture);
                }
                texture->setFrame(frameImage, bgra);
                node->frame->markDirty(QSGNode::DirtyMaterial);
            } else {
                QSGTexture *frameTexture = win->createTextureFromImage(frameImage);
                if (!frameTexture) {
                    if (uploaded)
                        resolveLater(std::move(uploaded->done), {m_shown.generation, PresentOutcome::Error,
                                                                 PresentStage::Submitted, "texture upload failed"});
                    return node;
                }
                frameTexture->setFiltering(QSGTexture::Linear);
                node->frame->setTexture(frameTexture);
                std::lock_guard lock(m_stats->mutex);
                ++m_stats->frameUploads;
                m_stats->lastFrameBytes = static_cast<std::size_t>(f.width) * f.height * 4;
            }
            node->frameLease = m_shown.frame;
            node->bgraLease = std::move(bgra);
        }
        // The overlay: none, or nothing visible, draws nothing.
        const auto overlay = m_shown.overlay && !m_shown.overlay->empty ? m_shown.overlay : nullptr;
        std::optional<QRect> bounds;
        if (overlay && m_shown.overlayBoundsKnown)
            bounds = rectOf(m_shown.overlayBounds);
        else if (overlay && !inPlace)
            bounds = rectOf(application::overlayBounds(*overlay));
        node->overlayLease = nullptr;
        if (overlay && !(bounds && bounds->isEmpty())) {
            const auto &o = *overlay;
            const QSize size(o.width, o.height);
            if (inPlace) {
                auto *texture = dynamic_cast<UpdatableTexture *>(node->overlay->texture());
                if (!texture) {
                    texture = new UpdatableTexture(
                        true,
                        [stats = m_stats](const QRect &region) {
                            std::lock_guard lock(stats->mutex);
                            ++stats->overlayUploads;
                            stats->lastOverlayUpload = region;
                        },
                        [stats = m_stats] {
                            std::lock_guard lock(stats->mutex);
                            stats->failed = true;
                        });
                    texture->setFiltering(QSGTexture::Linear);
                    node->overlay->setTexture(texture);
                }
                texture->setOverlay(QImage(o.pixels.data(), o.width, o.height, o.stride,
                                           QImage::Format_ARGB32_Premultiplied),
                                    overlay, bounds);
                node->overlay->markDirty(QSGNode::DirtyMaterial);
                node->overlayRegion = QRect(QPoint(0, 0), size);
                node->overlayLease = overlay;
            } else {
                // Only the part with visible pixels becomes an image.
                const QRect region = padded(*bounds, size);
                const QImage part(o.pixels.data() + static_cast<std::size_t>(region.y()) * o.stride + region.x() * 4,
                                  region.width(), region.height(), o.stride, QImage::Format_ARGB32_Premultiplied);
                QSGTexture *texture = win->createTextureFromImage(part, QQuickWindow::TextureHasAlphaChannel);
                if (!texture) {
                    if (uploaded)
                        resolveLater(std::move(uploaded->done), {m_shown.generation, PresentOutcome::Error,
                                                                 PresentStage::Submitted, "texture upload failed"});
                    return node;
                }
                texture->setFiltering(QSGTexture::Linear);
                node->overlay->setTexture(texture);
                node->overlayRegion = region;
                node->overlayLease = overlay;
                std::lock_guard lock(m_stats->mutex);
                ++m_stats->overlayUploads;
                m_stats->lastOverlayUpload = region;
            }
        }
    }
    const QRectF target = frameRect();
    node->background->setRect(boundingRect());
    // T1: the zoomed part of the frame (and of its overlay, made at its size).
    const QRectF whole(0, 0, m_shown.frame->width, m_shown.frame->height);
    const QRectF source = m_sourceRect.isEmpty() ? whole : m_sourceRect;
    if (node->ycbcrAttached) {
        node->ycbcr->place(target, source);
    } else {
        node->frame->setRect(target);
        node->frame->setSourceRect(source);
    }
    // The overlay's region of the frame, where the shown part of the frame covers it.
    QRectF visible;
    if (node->overlayLease && source.width() > 0 && source.height() > 0)
        visible = QRectF(node->overlayRegion).intersected(source);
    if (!visible.isEmpty()) {
        const double sx = target.width() / source.width(), sy = target.height() / source.height();
        node->overlay->setRect(QRectF(target.x() + (visible.x() - source.x()) * sx,
                                      target.y() + (visible.y() - source.y()) * sy, visible.width() * sx,
                                      visible.height() * sy));
        node->overlay->setSourceRect(visible.translated(-QPointF(node->overlayRegion.topLeft())));
        if (!node->overlayAttached) {
            node->appendChildNode(node->overlay);
            node->overlayAttached = true;
        }
    } else if (node->overlayAttached) {
        node->removeChildNode(node->overlay);
        node->overlayAttached = false;
    }
    node->markDirty(QSGNode::DirtyGeometry);

    if (uploaded) {
        std::lock_guard lock(m_renderMutex);
        if (m_awaitingRender)
            resolveLater(std::move(m_awaitingRender->done), {m_awaitingRender->result.generation,
                                                             PresentOutcome::Superseded, PresentStage::Uploaded, {}});
        m_awaitingRender = std::move(uploaded);
    }
    return node;
}

VideoPresenter::UploadStats VideoPresenter::uploadStats() const
{
    std::lock_guard lock(m_stats->mutex);
    return {m_stats->frameUploads,       m_stats->overlayUploads,     m_stats->lastOverlayUpload,
            m_stats->planarFrameUploads, m_stats->cpuConvertedFrames, m_stats->lastFrameBytes};
}

void VideoPresenter::onFrameSwapped()
{
    std::optional<Resolved> done;
    {
        std::lock_guard lock(m_renderMutex);
        done = std::exchange(m_awaitingRender, std::nullopt);
    }
    // A texture the frame just rendered could not be made (V1: textures
    // are made while rendering, after the sync that accepted the frame).
    bool failed = false;
    {
        std::lock_guard lock(m_stats->mutex);
        failed = std::exchange(m_stats->failed, false);
    }
    if (!done)
        return;
    if (failed) {
        done->result.outcome = PresentOutcome::Error;
        done->result.stage = PresentStage::Submitted;
        done->result.message = "texture upload failed";
        resolveLater(std::move(done->done), std::move(done->result));
        return;
    }
    const std::uint64_t generation = done->result.generation;
    QPointer<VideoPresenter> self(this);
    QMetaObject::invokeMethod(
        this,
        [self, generation, done = std::move(*done)]() mutable {
            if (self) {
                self->m_presentedGeneration = generation;
                emit self->presented();
            }
            done.result.outcome = PresentOutcome::Accepted;
            done.result.stage = PresentStage::Rendered;
            if (done.done)
                done.done(done.result);
        },
        Qt::QueuedConnection);
}

} // namespace hikari::ui
