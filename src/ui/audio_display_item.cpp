#include "audio_display_item.h"

#include "audio_controller.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QPainter>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <QSGTextNode>
#include <QTextLayout>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace hikari::ui {

using application::AudioShape;

namespace {

QColor colourOf(std::uint32_t argb)
{
    return QColor::fromRgba(argb);
}

// Direct3D 9 puts pixel centres at integers, Qt at half pixels.
constexpr float kPixelCentre = 0.5f;

// The shapes as triangles of one colour each and laid out texts, in draw
// order; the scene graph draws them as geometry (RHI) or a painted image
// (the software adaptation, which has no custom geometry).
struct Drawing {
    struct Triangles {
        std::uint32_t colour = 0;
        std::vector<QSGGeometry::Point2D> points;
    };
    struct Text {
        std::shared_ptr<QTextLayout> layout;
        QPointF at;
        QColor colour;
        bool outlined = false;
    };
    // A2: the spectrum, pixel for pixel
    struct Picture {
        QImage image;
        QPointF at;
    };
    std::vector<std::variant<Triangles, Text, Picture>> items;
};

class Batch {
public:
    explicit Batch(Drawing &out) : m_out(out) {}
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
    void flush(std::uint32_t colour)
    {
        if (m_points.empty())
            return;
        m_out.items.emplace_back(Drawing::Triangles{colour, std::move(m_points)});
        m_points.clear();
    }

private:
    Drawing &m_out;
    std::vector<QSGGeometry::Point2D> m_points;
};

Drawing describe(const std::vector<AudioShape> &shapes, const std::function<QFont(AudioShape::Font)> &fontOf)
{
    Drawing out;
    Batch batch(out);
    std::optional<std::uint32_t> colour;
    for (const auto &s : shapes) {
        if (s.kind == AudioShape::Kind::Image) {
            if (colour)
                batch.flush(*colour);
            colour.reset();
            if (s.image && s.image->width > 0 && s.image->height > 0) {
                // BGRA bytes are QImage's RGB32 on little-endian machines
                const QImage view(s.image->bgra.data(), s.image->width, s.image->height, s.image->width * 4,
                                  QImage::Format_RGB32);
                out.items.emplace_back(Drawing::Picture{view.copy(), QPointF(s.x1, s.y1)});
            }
            continue;
        }
        if (s.kind == AudioShape::Kind::Text) {
            if (colour)
                batch.flush(*colour);
            colour.reset();
            auto layout = std::make_shared<QTextLayout>(QString::fromStdString(s.text), fontOf(s.font));
            layout->beginLayout();
            QTextLine line = layout->createLine();
            line.setLineWidth(1e6);
            layout->endLayout();
            const qreal w = line.naturalTextWidth(), h = line.height();
            qreal x = s.x1, y = s.y1;
            if (s.align != AudioShape::Align::TopLeft)
                x = s.x1 + (s.x2 - s.x1 - w) / 2;
            if (s.align == AudioShape::Align::Center)
                y = s.y1 + (s.y2 - s.y1 - h) / 2;
            out.items.emplace_back(Drawing::Text{std::move(layout), QPointF(std::round(x), std::round(y)),
                                                 colourOf(s.colour), s.outlined});
            continue;
        }
        if (colour && *colour != s.colour)
            batch.flush(*colour);
        colour = s.colour;
        switch (s.kind) {
        case AudioShape::Kind::Fill: batch.rect(s.x1, s.y1, s.x2, s.y2); break;
        case AudioShape::Kind::Line: batch.line(s.x1, s.y1, s.x2, s.y2, s.width); break;
        case AudioShape::Kind::Triangle:
            batch.triangle(QPointF(s.x1, s.y1), QPointF(s.x2, s.y2), QPointF(s.x3, s.y3));
            break;
        case AudioShape::Kind::Text:
        case AudioShape::Kind::Image: break;
        }
    }
    if (colour)
        batch.flush(*colour);
    return out;
}

// Legacy DRAWOUTTEXT: the text eight times in black one pixel around, then
// in its colour.
template <typename Draw> void outlined(const Drawing::Text &text, Draw draw)
{
    if (text.outlined)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy)
                    draw(text.at + QPointF(dx, dy), QColor(Qt::black));
    draw(text.at, text.colour);
}

// The RHI path: geometry nodes and text nodes.
void buildNodes(QSGNode *parent, const Drawing &drawing, QQuickWindow *window)
{
    for (const auto &item : drawing.items) {
        if (const auto *t = std::get_if<Drawing::Triangles>(&item)) {
            auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), static_cast<int>(t->points.size()));
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            std::copy(t->points.begin(), t->points.end(), geometry->vertexDataAsPoint2D());
            auto *material = new QSGFlatColorMaterial;
            material->setColor(colourOf(t->colour));
            auto *node = new QSGGeometryNode;
            node->setGeometry(geometry);
            node->setMaterial(material);
            node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
            parent->appendChildNode(node);
            continue;
        }
        if (const auto *p = std::get_if<Drawing::Picture>(&item)) {
            if (!window)
                continue;
            QSGImageNode *node = window->createImageNode();
            node->setTexture(window->createTextureFromImage(p->image));
            node->setOwnsTexture(true);
            node->setFiltering(QSGTexture::Nearest);
            node->setRect(QRectF(p->at, QSizeF(p->image.size())));
            parent->appendChildNode(node);
            continue;
        }
        const auto &text = std::get<Drawing::Text>(item);
        if (!window)
            continue;
        outlined(text, [&](QPointF at, const QColor &colour) {
            QSGTextNode *node = window->createTextNode();
            node->setColor(colour);
            node->addTextLayout(at, text.layout.get());
            parent->appendChildNode(node);
        });
    }
}

// The software path: the same triangles filled where they cover a pixel's
// centre (no antialiasing, as the geometry is rasterized) and the same texts.
QImage paintImage(const Drawing &drawing, QSize size, qreal dpr)
{
    QImage image((QSizeF(size) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    for (const auto &item : drawing.items) {
        if (const auto *t = std::get_if<Drawing::Triangles>(&item)) {
            painter.setBrush(colourOf(t->colour));
            for (std::size_t i = 0; i + 2 < t->points.size(); i += 3) {
                const QPointF tri[3] = {{t->points[i].x, t->points[i].y},
                                        {t->points[i + 1].x, t->points[i + 1].y},
                                        {t->points[i + 2].x, t->points[i + 2].y}};
                painter.drawConvexPolygon(tri, 3);
            }
            continue;
        }
        if (const auto *p = std::get_if<Drawing::Picture>(&item)) {
            painter.drawImage(p->at, p->image);
            continue;
        }
        const auto &text = std::get<Drawing::Text>(item);
        outlined(text, [&](QPointF at, const QColor &colour) {
            painter.setPen(colour);
            text.layout->draw(&painter, at);
            painter.setPen(Qt::NoPen);
        });
    }
    return image;
}

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

bool softwareScene(QQuickWindow *window)
{
    return window && window->rendererInterface() &&
           window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
}

// The RHI path: `shapes` as geometry and text nodes under `parent`.
void build(QSGNode *parent, const std::vector<AudioShape> &shapes, QQuickWindow *window,
           const std::function<QFont(AudioShape::Font)> &fontOf)
{
    buildNodes(parent, describe(shapes, fontOf), window);
}

// A5: legacy iswctype(ch, _SPACE) and (ch, _SPACE | _PUNCT) as the Windows
// CRT answers them: ASCII by the C tables; past it GetStringTypeW's C1_SPACE
// and C1_PUNCT, Unicode's spaces and its punctuation and symbol categories.
application::KaraokeCharClass karaokeCharClass()
{
    const auto ascii = application::KaraokeCharClass::ascii();
    application::KaraokeCharClass classes;
    classes.space = [ascii](char16_t c) { return c < 128 ? ascii.space(c) : QChar(c).isSpace(); };
    classes.punct = [ascii](char16_t c) {
        if (c < 128)
            return ascii.punct(c);
        const QChar ch(c);
        return ch.isPunct() || ch.isSymbol();
    };
    return classes;
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
    // legacy D3DXCreateFontW: tahoma13 and verdana11 are FW_BOLD, tahoma8 FW_NORMAL
    m_cursor.setBold(true);
    m_label.setBold(true);
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
        m_controller->setMarkTextHeight(QFontMetrics(m_label).height()); // A3
        // A5: legacy GetTextExtentPixel's font (verdana11, the label font):
        // D3DX measured without the leading and trailing spaces
        m_controller->setKaraokeMeasure(application::karaokeLabelMeasure(
            [label = m_label](AudioShape::Font, std::string_view text) {
                return QFontMetrics(label).horizontalAdvance(
                    QString::fromUtf8(text.data(), qsizetype(text.size())));
            }));
        m_controller->setKaraokeClasses(karaokeCharClass());
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
    const bool over = p.x() >= 0 && p.y() >= 0 && p.x() < width() && p.y() < h;
    if (over && !hasActiveFocus())
        forceActiveFocus(Qt::MouseFocusReason);
    const auto result = timingEvent(event, static_cast<int>(application::AudioMouse::Type::Move));
    // A5: in karaoke mode legacy can return before the cursor is drawn, and
    // draws none over the syllables' letters
    if (result.keepCursor)
        return;
    if (over && !result.hideCursor)
        m_controller->setCursor(static_cast<float>(static_cast<int>(p.x())));
    else
        m_controller->setCursor(std::nullopt);
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
    timingEvent(event, static_cast<int>(application::AudioMouse::Type::Press));
    event->accept();
}

// A3: the buttons' timing (legacy OnMouseEvent: the display has the mouse
// captured while a button is down).
void AudioDisplayItem::mouseMoveEvent(QMouseEvent *event)
{
    timingEvent(event, static_cast<int>(application::AudioMouse::Type::Move));
    event->accept();
}

void AudioDisplayItem::mouseReleaseEvent(QMouseEvent *event)
{
    timingEvent(event, static_cast<int>(application::AudioMouse::Type::Release));
    event->accept();
}

void AudioDisplayItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    timingEvent(event, static_cast<int>(application::AudioMouse::Type::DoubleClick));
    event->accept();
}

void AudioDisplayItem::mouseUngrabEvent()
{
    if (m_controller)
        m_controller->lostCapture();
}

application::AudioMouseResult AudioDisplayItem::timingEvent(const QSinglePointEvent *event, int type)
{
    if (!m_controller)
        return {};
    using Mouse = application::AudioMouse;
    Mouse mouse;
    mouse.type = static_cast<Mouse::Type>(type);
    switch (event->button()) {
    case Qt::LeftButton: mouse.button = Mouse::Button::Left; break;
    case Qt::RightButton: mouse.button = Mouse::Button::Right; break;
    case Qt::MiddleButton: mouse.button = Mouse::Button::Middle; break;
    default: break;
    }
    const QPointF p = event->position();
    mouse.x = static_cast<int>(std::floor(p.x()));
    mouse.y = static_cast<int>(std::floor(p.y()));
    const auto buttons = event->buttons();
    mouse.leftHeld = buttons & Qt::LeftButton;
    mouse.rightHeld = buttons & Qt::RightButton;
    mouse.middleHeld = buttons & Qt::MiddleButton;
    const auto modifiers = event->modifiers();
    mouse.shift = modifiers & Qt::ShiftModifier;
    mouse.ctrl = modifiers & Qt::ControlModifier;
    mouse.alt = modifiers & Qt::AltModifier;
    m_controller->setMarkTextHeight(QFontMetrics(m_label).height());
    const auto result = m_controller->mouse(mouse);
    if (result.focus && !hasActiveFocus()) // legacy SetFocus on a button (and the middle double click)
        forceActiveFocus(Qt::MouseFocusReason);
    if (result.sizeCursor) {
        if (*result.sizeCursor)
            setCursor(Qt::SizeHorCursor); // wxCURSOR_SIZEWE
        else
            unsetCursor();
    }
    return result;
}

// Legacy OnMouseEvent's wheel: Ctrl alone zooms vertically, Shift
// horizontally around the mouse (A2), otherwise it scrolls. Legacy read
// GetWheelRotation() without GetWheelAxis(), so a horizontal wheel acts as
// the vertical one with its rotation; on Windows (the normative build) that
// rotation is positive for a tilt to the right, which Qt reports as a
// negative x (QWindowsMouseHandler reverses WM_MOUSEHWHEEL's delta).
void AudioDisplayItem::wheelEvent(QWheelEvent *event)
{
    const QPoint delta = event->angleDelta();
    const int rotation = delta.y() != 0 ? delta.y() : -delta.x();
    if (m_controller && rotation != 0) {
        const auto modifiers = event->modifiers();
        m_controller->wheel(rotation, modifiers == Qt::ControlModifier, modifiers.testFlag(Qt::ShiftModifier),
                            static_cast<float>(static_cast<int>(event->position().x())));
    }
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

QSGNode *AudioDisplayItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto fontOf = [this](AudioShape::Font f) { return font(static_cast<int>(f)); };
    auto sceneShapes = [this] {
        return m_controller->scene([this](AudioShape::Font f, std::string_view text) {
            return QFontMetrics(font(static_cast<int>(f))).horizontalAdvance(QString::fromUtf8(text.data(), qsizetype(text.size())));
        });
    };
    auto cursorShapes = [this] {
        return m_controller->cursor() && m_controller->ready()
                   ? application::audioCursor(m_controller->view(), *m_controller->cursor(), m_controller->playing(),
                                                         m_controller->options(), m_controller->karaoke())
                   : std::vector<AudioShape>{};
    };
    if (softwareScene(window())) {
        // The software adaptation: one image node, the scene painted once per
        // revision and the cursor painted over a copy of it.
        auto *node = static_cast<QSGImageNode *>(oldNode);
        if (!node) {
            node = window()->createImageNode();
            node->setOwnsTexture(true);
            node->setFiltering(QSGTexture::Nearest);
            m_drawnRevision = 0;
        }
        const QSize pixels = size().toSize();
        if (!m_controller || pixels.isEmpty()) {
            node->setTexture(window()->createTextureFromImage(QImage(1, 1, QImage::Format_ARGB32_Premultiplied)));
            node->setRect(QRectF());
            return node;
        }
        const qreal dpr = window()->effectiveDevicePixelRatio();
        if (m_drawnRevision != m_controller->revision() || m_sceneImage.deviceIndependentSize().toSize() != pixels) {
            m_drawnRevision = m_controller->revision();
            m_sceneImage = paintImage(describe(sceneShapes(), fontOf), pixels, dpr);
        }
        QImage image = m_sceneImage;
        if (const auto cursor = cursorShapes(); !cursor.empty()) {
            const QImage over = paintImage(describe(cursor, fontOf), pixels, dpr);
            QPainter painter(&image);
            painter.drawImage(QPointF(0, 0), over);
        }
        QSGTexture *texture = window()->createTextureFromImage(image);
        node->setTexture(texture);
        node->setSourceRect(QRectF(QPointF(0, 0), texture->textureSize()));
        node->setRect(QRectF(QPointF(0, 0), size()));
        return node;
    }
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
    if (m_drawnRevision != m_controller->revision()) {
        m_drawnRevision = m_controller->revision();
        clear(node->image);
        build(node->image, sceneShapes(), window(), fontOf);
    }
    clear(node->cursor);
    build(node->cursor, cursorShapes(), window(), fontOf);
    return node;
}

} // namespace hikari::ui
