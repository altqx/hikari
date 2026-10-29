#include "shape_editor.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hikari::ui {

using application::visual::ShapePreset;
using application::visual::ShapesEdition;

namespace {

QString qs(std::u16string_view text)
{
    return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
}

const QSet<QString> &defaultNames()
{
    static const QSet<QString> names = [] {
        QSet<QString> out;
        for (const ShapePreset &p : application::visual::defaultShapePresets())
            out.insert(qs(p.name));
        return out;
    }();
    return names;
}

}

ShapeEditor::ShapeEditor(std::vector<ShapePreset> presets, int curShape, Hooks hooks, QObject *parent)
    : QObject(parent), m_edition(std::move(presets), curShape), m_hooks(std::move(hooks))
{
}

QString ShapeEditor::svgPath(std::u16string_view drawing, QRectF *bounds)
{
    QString out;
    QChar command;
    std::vector<double> numbers;
    double x0 = std::numeric_limits<double>::max(), y0 = x0;
    double x1 = std::numeric_limits<double>::lowest(), y1 = x1;
    const auto point = [&](QPointF p) {
        x0 = std::min(x0, p.x());
        y0 = std::min(y0, p.y());
        x1 = std::max(x1, p.x());
        y1 = std::max(y1, p.y());
        return QString::number(p.x()) + QLatin1Char(' ') + QString::number(p.y());
    };
    QPointF pen;
    std::vector<QPointF> spline;
    // libass draws s and p as a uniform cubic B-spline; c closes it with its first three points again.
    const auto drawSpline = [&](bool close) {
        if (close && spline.size() >= 3)
            spline.insert(spline.end(), {spline[0], spline[1], spline[2]});
        for (std::size_t i = 0; i + 3 < spline.size(); ++i) {
            const QPointF &a = spline[i], &b = spline[i + 1], &c = spline[i + 2], &d = spline[i + 3];
            if (i == 0)
                out += QStringLiteral(" L ") + point((a + 4 * b + c) / 6);
            out += QStringLiteral(" C ") + point((2 * b + c) / 3) + QLatin1Char(' ') + point((b + 2 * c) / 3) +
                   QLatin1Char(' ') + point((b + 4 * c + d) / 6);
        }
        spline.clear();
    };
    const auto flush = [&] {
        const std::size_t pairs = numbers.size() / 2;
        const auto at = [&](std::size_t i) { return QPointF(numbers[2 * i], numbers[2 * i + 1]); };
        const char16_t c = command.unicode();
        if (c != u'p' && c != u'c')
            drawSpline(false);
        if ((c == u'm' || c == u'n') && pairs > 0) {
            out += QStringLiteral(" M ") + point(at(0));
            for (std::size_t i = 1; i < pairs; ++i)
                out += QStringLiteral(" L ") + point(at(i));
        } else if (c == u'l') {
            for (std::size_t i = 0; i < pairs; ++i)
                out += QStringLiteral(" L ") + point(at(i));
        } else if (c == u's' || (c == u'p' && !spline.empty())) {
            if (c == u's')
                spline.push_back(pen);
            for (std::size_t i = 0; i < pairs; ++i)
                spline.push_back(at(i));
        } else if (c == u'b') {
            for (std::size_t i = 0; i + 2 < pairs; i += 3)
                out += QStringLiteral(" C ") + point(at(i)) + QLatin1Char(' ') + point(at(i + 1)) + QLatin1Char(' ') +
                       point(at(i + 2));
        } else if (c == u'c') {
            drawSpline(true);
            out += QStringLiteral(" Z");
        }
        if (pairs > 0 && c != u'c')
            pen = at(pairs - 1);
        numbers.clear();
    };
    const QString text = qs(drawing);
    // Tokens that are neither a command nor a number are skipped, as libass skips them.
    for (const QString &token : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        if (token.size() == 1 && QStringLiteral("mnlbspc").contains(token[0])) {
            flush();
            command = token[0];
            continue;
        }
        bool ok = false;
        const double v = token.toDouble(&ok);
        if (ok && std::isfinite(v))
            numbers.push_back(v);
    }
    flush();
    drawSpline(false);
    if (bounds)
        *bounds = x0 <= x1 ? QRectF(QPointF(x0, y0), QPointF(x1, y1)) : QRectF();
    return out.trimmed();
}

QVariantList ShapeEditor::items() const
{
    QVariantList out;
    for (const ShapePreset &p : m_edition.presets()) {
        QRectF bounds;
        const QString path = svgPath(p.shape, &bounds);
        out.append(QVariantMap{{QStringLiteral("name"), qs(p.name)},
                               {QStringLiteral("path"), path},
                               {QStringLiteral("box"), bounds},
                               {QStringLiteral("own"), !defaultNames().contains(qs(p.name))}});
    }
    return out;
}

void ShapeEditor::select(int index)
{
    if (index == selection())
        return;
    m_edition.select(index);
    emit changed();
}

QString ShapeEditor::name() const
{
    return qs(m_edition.current().name);
}

QString ShapeEditor::shape() const
{
    return qs(m_edition.current().shape);
}

QString ShapeEditor::path() const
{
    return svgPath(m_edition.current().shape);
}

QRectF ShapeEditor::box() const
{
    QRectF bounds;
    (void)svgPath(m_edition.current().shape, &bounds);
    return bounds;
}

int ShapeEditor::ownCount() const
{
    return static_cast<int>(std::ranges::count_if(m_edition.presets(), [](const ShapePreset &p) {
        return !defaultNames().contains(qs(p.name));
    }));
}

void ShapeEditor::kept()
{
    emit changed();
    if (m_edition.revision() == m_published)
        return;
    m_published = m_edition.revision();
    if (m_hooks.changed)
        m_hooks.changed(m_edition.presets(), m_edition.renamed());
}

void ShapeEditor::setMode(int mode)
{
    if (mode == this->mode())
        return;
    m_edition.setMode(mode);
    kept();
}

int ShapeEditor::setName(const QString &name)
{
    if (name == this->name())
        return NameOk;
    std::u16string taken;
    const auto check = m_edition.setName(name.toStdU16String(), &taken);
    m_taken = qs(taken);
    if (check != ShapesEdition::NameCheck::Ok)
        return static_cast<int>(check);
    kept();
    return NameOk;
}

bool ShapeEditor::setShape(const QString &shape)
{
    if (shape == this->shape())
        return true;
    if (!m_edition.setShape(shape.toStdU16String()))
        return false;
    kept();
    return true;
}

void ShapeEditor::addShape()
{
    m_edition.addShape(m_hooks.activeLineText ? m_hooks.activeLineText() : std::u16string());
    kept();
}

void ShapeEditor::duplicate()
{
    m_edition.duplicate();
    kept();
}

bool ShapeEditor::remove()
{
    if (!m_edition.remove())
        return false;
    kept();
    return true;
}

bool ShapeEditor::takeFromLine()
{
    if (!m_edition.takeFromLine(m_hooks.activeLineText ? m_hooks.activeLineText() : std::u16string()))
        return false;
    kept();
    return true;
}

void ShapeEditor::replaceNamed(const QString &name)
{
    m_edition.replaceNamed(name.toStdU16String());
    kept();
}

void ShapeEditor::restoreDefaults()
{
    m_edition.restoreDefaults(m_hooks.defaults ? m_hooks.defaults() : application::visual::defaultShapePresets());
    kept();
}

bool ShapeEditor::undoAll()
{
    if (!m_edition.undoAll())
        return false;
    kept();
    return true;
}

void ShapeEditor::undo()
{
    if (m_edition.undo())
        kept();
}

void ShapeEditor::redo()
{
    if (m_edition.redo())
        kept();
}

void ShapeEditor::close()
{
    if (m_done)
        return;
    m_done = true;
    if (m_hooks.finished)
        m_hooks.finished();
    emit closed();
}

}
