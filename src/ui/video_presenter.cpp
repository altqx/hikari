#include "video_presenter.h"

#include <QImage>
#include <QPointer>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

namespace hikari::ui {

using application::PresentOutcome;
using application::PresentResult;
using application::PresentStage;

namespace {

// Holds the leases its textures were made from.
class PresentNode : public QSGNode {
public:
    QSGRectangleNode *background = nullptr;
    QSGSimpleTextureNode *frame = nullptr;
    QSGSimpleTextureNode *overlay = nullptr;
    std::shared_ptr<const application::IndexedFrame> frameLease;
    std::shared_ptr<const application::OverlayFrame> overlayLease;
};

QString layoutError(const application::Presentation &p)
{
    const auto &f = p.frame;
    if (!f)
        return QStringLiteral("no frame");
    if (f->width <= 0 || f->height <= 0 || f->stride < f->width * 4 ||
        f->bgra.size() < static_cast<std::size_t>(f->stride) * static_cast<std::size_t>(f->height))
        return QStringLiteral("frame layout does not match its buffer");
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
        node->appendChildNode(node->overlay);
    }
    if (upload) {
        const auto &f = *m_shown.frame;
        const QImage frameImage(reinterpret_cast<const uchar *>(f.bgra.data()), f.width, f.height, f.stride,
                                QImage::Format_RGB32);
        QSGTexture *frameTexture = win->createTextureFromImage(frameImage);
        QSGTexture *overlayTexture = nullptr;
        if (m_shown.overlay && !m_shown.overlay->empty) {
            const auto &o = *m_shown.overlay;
            const QImage overlayImage(o.pixels.data(), o.width, o.height, o.stride,
                                      QImage::Format_ARGB32_Premultiplied);
            overlayTexture = win->createTextureFromImage(overlayImage, QQuickWindow::TextureHasAlphaChannel);
        }
        if (!frameTexture || (m_shown.overlay && !m_shown.overlay->empty && !overlayTexture)) {
            delete frameTexture;
            delete overlayTexture;
            if (uploaded)
                resolveLater(std::move(uploaded->done), {m_shown.generation, PresentOutcome::Error,
                                                         PresentStage::Submitted, "texture upload failed"});
            return node;
        }
        frameTexture->setFiltering(QSGTexture::Linear);
        node->frame->setTexture(frameTexture);
        node->frameLease = m_shown.frame;
        node->overlay->setTexture(overlayTexture ? overlayTexture : win->createTextureFromImage(QImage(1, 1, QImage::Format_ARGB32_Premultiplied)));
        if (overlayTexture)
            overlayTexture->setFiltering(QSGTexture::Linear);
        node->overlayLease = overlayTexture ? m_shown.overlay : nullptr;
    }
    const QRectF target = frameRect();
    node->background->setRect(boundingRect());
    node->frame->setRect(target);
    node->overlay->setRect(node->overlayLease ? target : QRectF());
    // T1: the zoomed part of the frame (and of its overlay, made at its size).
    const QRectF whole(0, 0, m_shown.frame->width, m_shown.frame->height);
    const QRectF source = m_sourceRect.isEmpty() ? whole : m_sourceRect;
    node->frame->setSourceRect(source);
    if (node->overlayLease)
        node->overlay->setSourceRect(source);
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

void VideoPresenter::onFrameSwapped()
{
    std::optional<Resolved> done;
    {
        std::lock_guard lock(m_renderMutex);
        done = std::exchange(m_awaitingRender, std::nullopt);
    }
    if (!done)
        return;
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
