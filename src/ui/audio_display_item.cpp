#include "audio_display_item.h"

#include "audio_controller.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGTextNode>
#include <QTextLayout>

#include <cmath>
#include <functional>
#include <optional>

namespace hikari::ui {

using application::AudioShape;

namespace {

QColor colourOf(std::uint32_t argb)
{
    return QColor::fromRgba(argb);
}

// Direct3D 9 puts pixel centres at integers, Qt at half pixels.
constexpr float kPixelCentre = 0.5f;

class Batch {
public:
    void triangle(QPointF a, QPointF b, QPointF c)
    {
        for (QPointF p : {a, b, c})
            m_points.push_back({float(p.x()) + kPixelCentre, float(p.y()) + kPixelCentre});
    }
    void rect(float x1, float y1, float x2, float y2)
    {
        const QPointF a(std::min(x1, x2), std::min(y1, y2)), b(std::max(x1, x2), std::max(y1, y2));
        triangle(a, QPointF(b.x(), a.y()), b);
        triangle(a, b, QPointF(a.x(), b.y()));
    }
    // A D3DXLine: `width` wide, centred on the segment, no caps.
    void line(float x1, float y1, float x2, float y2, float width)
    {
        const float dx = x2 - x1, dy = y2 - y1;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length == 0)
            return;
        const float nx = -dy / length * width / 2, ny = dx / length * width / 2;
        const QPointF a(x1 + nx, y1 + ny), b(x1 - nx, y1 - ny), c(x2 - nx, y2 - ny), d(x2 + nx, y2 + ny);
        triangle(a, b, c);
        triangle(a, c, d);
    }
    // Flushes into a node under `parent`.
    void flush(QSGNode *parent, std::uint32_t colour)
    {
        if (m_points.empty())
            return;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), static_cast<int>(m_points.size()));
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        std::copy(m_points.begin(), m_points.end(), geometry->vertexDataAsPoint2D());
        auto *material = new QSGFlatColorMaterial;
        material->setColor(colourOf(colour));
        auto *node = new QSGGeometryNode;
        node->setGeometry(geometry);
        node->setMaterial(material);
        node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        parent->appendChildNode(node);
        m_points.clear();
    }

private:
    std::vector<QSGGeometry::Point2D> m_points;
};

// The image and the cursor, each rebuilt on its own.
class DisplayNode : public QSGNode {
public:
    QSGNode *image = new QSGNode;
    QSGNode *cursor = new QSGNode;
    DisplayNode()
    {
        appendChildNode(image);
        appendChildNode(cursor);
    }
};

void clear(QSGNode *node)
{
    while (QSGNode *child = node->firstChild()) {
        node->removeChildNode(child);
        delete child;
    }
}

} // namespace

AudioDisplayItem::AudioDisplayItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setActiveFocusOnTab(true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::AllButtons);
    const QFont base = QGuiApplication::font();
    const qreal size = base.pointSizeF() > 0 ? base.pointSizeF() : 9;
    m_scale = m_cursor = m_label = base;
    m_scale.setPointSizeF(size - 1);
    m_cursor.setPointSizeF(size + 3);
    m_label.setPointSizeF(size + 1);
}

QObject *AudioDisplayItem::controller() const
{
    return m_controller;
}

void AudioDisplayItem::setController(QObject *object)
{
    auto *controller = qobject_cast<AudioController *>(object);
    if (controller == m_controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &AudioController::displayChanged, this, &QQuickItem::update);
        connect(m_controller, &AudioController::cursorChanged, this, &QQuickItem::update);
        pushSize();
    }
    emit controllerChanged();
    update();
}

QFont AudioDisplayItem::font(int role) const
{
    switch (static_cast<AudioShape::Font>(role)) {
    case AudioShape::Font::Scale: return m_scale;
    case AudioShape::Font::Cursor: return m_cursor;
    case AudioShape::Font::Label: return m_label;
    }
    return m_scale;
}

// Legacy: the height of "#TWFfGH" in the ruler's font, plus 8.
int AudioDisplayItem::timelineHeight() const
{
    return QFontMetrics(m_scale).height() + 8;
}

void AudioDisplayItem::pushSize()
{
    if (!m_controller)
        return;
    // legacy HikariScrollbar::CalculateThickness: the program font's height + 1
    const int scrollbar = QFontMetrics(QGuiApplication::font()).height() + 1;
    m_controller->resize(static_cast<int>(width()), static_cast<int>(height()), timelineHeight(), scrollbar);
}

void AudioDisplayItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        pushSize();
}

// Legacy OnMouseEvent's cursor drawing: over the waveform the cursor follows
// the mouse (and the display takes focus, AUDIO_AUTO_FOCUS); over the ruler
// it is not drawn.
void AudioDisplayItem::hoverMoveEvent(QHoverEvent *event)
{
    if (!m_controller)
        return;
    const QPointF p = event->position();
    const int h = m_controller->view().height();
    if (p.x() >= 0 && p.y() >= 0 && p.x() < width() && p.y() < h) {
        if (!hasActiveFocus())
            forceActiveFocus(Qt::MouseFocusReason);
        m_controller->setCursor(static_cast<float>(static_cast<int>(p.x())));
    } else {
        m_controller->setCursor(std::nullopt);
    }
}

void AudioDisplayItem::hoverLeaveEvent(QHoverEvent *)
{
    if (m_controller)
        m_controller->setCursor(std::nullopt);
}

// Any button takes focus and hides the cursor (timing clicks are A3's).
void AudioDisplayItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (m_controller)
        m_controller->setCursor(std::nullopt);
    event->accept();
}

void AudioDisplayItem::wheelEvent(QWheelEvent *event)
{
    // Shift and Ctrl zoom (A2)
    if (m_controller && event->modifiers() == Qt::NoModifier && event->angleDelta().y() != 0)
        m_controller->wheel(event->angleDelta().y());
    event->accept();
}

void AudioDisplayItem::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    if (m_controller)
        m_controller->setFocused(true);
}

void AudioDisplayItem::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    if (m_controller)
        m_controller->setFocused(false);
}

namespace {

void build(QSGNode *parent, const std::vector<AudioShape> &shapes, QQuickWindow *window,
           const std::function<QFont(AudioShape::Font)> &fontOf)
{
    Batch batch;
    std::optional<std::uint32_t> colour;
    for (const auto &s : shapes) {
        if (s.kind == AudioShape::Kind::Text) {
            if (colour)
                batch.flush(parent, *colour);
            colour.reset();
            if (!window)
                continue;
            QTextLayout layout(QString::fromStdString(s.text), fontOf(s.font));
            layout.beginLayout();
            QTextLine line = layout.createLine();
            line.setLineWidth(1e6);
            layout.endLayout();
            const qreal w = line.naturalTextWidth(), h = line.height();
            qreal x = s.x1, y = s.y1;
            if (s.align != AudioShape::Align::TopLeft)
                x = s.x1 + (s.x2 - s.x1 - w) / 2;
            if (s.align == AudioShape::Align::Center)
                y = s.y1 + (s.y2 - s.y1 - h) / 2;
            QSGTextNode *text = window->createTextNode();
            text->setColor(colourOf(s.colour));
            if (s.outlined) {
                text->setTextStyle(QSGTextNode::Outline);
                text->setStyleColor(Qt::black);
            }
            text->addTextLayout(QPointF(std::round(x), std::round(y)), &layout);
            parent->appendChildNode(text);
            continue;
        }
        if (colour && *colour != s.colour)
            batch.flush(parent, *colour);
        colour = s.colour;
        switch (s.kind) {
        case AudioShape::Kind::Fill: batch.rect(s.x1, s.y1, s.x2, s.y2); break;
        case AudioShape::Kind::Line: batch.line(s.x1, s.y1, s.x2, s.y2, s.width); break;
        case AudioShape::Kind::Triangle:
            batch.triangle(QPointF(s.x1, s.y1), QPointF(s.x2, s.y2), QPointF(s.x3, s.y3));
            break;
        case AudioShape::Kind::Text: break;
        }
    }
    if (colour)
        batch.flush(parent, *colour);
}

} // namespace

QSGNode *AudioDisplayItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<DisplayNode *>(oldNode);
    if (!node) {
        node = new DisplayNode;
        m_drawnRevision = 0;
    }
    if (!m_controller) {
        clear(node->image);
        clear(node->cursor);
        return node;
    }
    auto fontOf = [this](AudioShape::Font f) { return font(static_cast<int>(f)); };
    if (m_drawnRevision != m_controller->revision()) {
        m_drawnRevision = m_controller->revision();
        clear(node->image);
        const auto shapes = m_controller->scene([this](AudioShape::Font f, std::string_view text) {
            return QFontMetrics(font(static_cast<int>(f))).horizontalAdvance(QString::fromUtf8(text.data(), qsizetype(text.size())));
        });
        build(node->image, shapes, window(), fontOf);
    }
    clear(node->cursor);
    if (m_controller->cursor() && m_controller->ready())
        build(node->cursor, application::audioCursor(m_controller->view(), *m_controller->cursor(), false,
                                                     m_controller->options()),
              window(), fontOf);
    return node;
}

} // namespace hikari::ui
