#include "shape_editor.h"

namespace hikari::ui {

using application::visual::ShapePreset;
using application::visual::ShapesEdition;

namespace {

QString qs(std::u16string_view text)
{
    return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
}

QVariantMap message(const std::optional<ShapesEdition::Message> &m)
{
    if (!m)
        return {};
    return {{QStringLiteral("text"), qs(m->text)}, {QStringLiteral("title"), qs(m->title)}};
}

} // namespace

ShapeEditor::ShapeEditor(std::vector<ShapePreset> presets, int curShape, Hooks hooks, QObject *parent)
    : QObject(parent), m_edition(std::move(presets), curShape), m_hooks(std::move(hooks))
{
}

QStringList ShapeEditor::list() const
{
    QStringList out;
    for (const auto &n : m_edition.list())
        out.append(qs(n));
    return out;
}

QString ShapeEditor::name() const
{
    return qs(m_edition.name);
}

QString ShapeEditor::shape() const
{
    return qs(m_edition.shape);
}

void ShapeEditor::setName(const QString &name)
{
    if (name == this->name())
        return;
    m_edition.name = name.toStdU16String();
    emit changed();
}

void ShapeEditor::setShape(const QString &shape)
{
    if (shape == this->shape())
        return;
    m_edition.shape = shape.toStdU16String();
    emit changed();
}

void ShapeEditor::setMode(int mode)
{
    if (mode == m_edition.mode)
        return;
    m_edition.mode = mode;
    emit changed();
}

void ShapeEditor::setScalingMode(int mode)
{
    if (mode == m_edition.scalingMode)
        return;
    m_edition.scalingMode = mode;
    emit changed();
}

QVariantMap ShapeEditor::addShape(const QString &newName)
{
    const auto m = m_edition.addShape(newName.toStdU16String());
    emit changed();
    return message(m);
}

QVariantMap ShapeEditor::removeShape()
{
    const auto m = m_edition.removeShape();
    emit changed();
    return message(m);
}

QVariantMap ShapeEditor::listChangeQuestion() const
{
    if (!m_edition.modified())
        return {};
    return message(m_edition.saveChangesQuestion());
}

void ShapeEditor::select(int index)
{
    m_edition.select(index);
    emit changed();
}

bool ShapeEditor::getShapeFromLine()
{
    const bool found = m_edition.getShapeFromLine(m_hooks.activeLineText ? m_hooks.activeLineText() : u"");
    emit changed();
    return found;
}

QVariantMap ShapeEditor::save(bool ok)
{
    const ShapesEdition::SaveResult r = m_edition.save();
    emit changed();
    if (r.error)
        return message(r.error);
    if (r.clash)
        return {{QStringLiteral("clash"), qs(*r.clash)}};
    if (ok)
        finish(true); // SaveSettings and EndModal(wxID_OK)
    return {};
}

int ShapeEditor::replaceClash(int pending, bool ok)
{
    (void)m_edition.replaceClash(&pending);
    emit changed();
    if (ok)
        finish(true);
    return pending;
}

QVariantMap ShapeEditor::restoreQuestion() const
{
    return message(ShapesEdition::restoreQuestion());
}

void ShapeEditor::restoreDefaults()
{
    // OnResetDefault after Yes: the file goes at once (Cancel does not bring
    // it back), then LoadSettings gives legacy's defaults.
    if (m_hooks.removeFile)
        m_hooks.removeFile();
    m_edition.restoreDefaults(m_hooks.defaults ? m_hooks.defaults() : application::visual::defaultShapePresets());
    emit changed();
}

void ShapeEditor::cancel()
{
    finish(false);
}

void ShapeEditor::finish(bool ok)
{
    if (m_done)
        return;
    m_done = true;
    if (m_hooks.finished)
        m_hooks.finished(ok ? std::optional(m_edition.presets()) : std::nullopt);
    emit closed();
}

} // namespace hikari::ui
