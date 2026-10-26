#pragma once

// PU5 (#307): the "Edit shapes" window's model for QML (legacy ShapesEdition,
// "Vector shape editing", HikariSub/VisualDrawingShapes.cpp:43-288,
// redesigned): the drawing tool's shape presets, edited through
// application::visual::ShapesEdition. The VisualToolsController opens it
// from the shape list's "Edit shapes…" and takes the presets back after
// every change (hooks.changed: the tool's list and the file), until the
// window closes (hooks.finished). The window words the steps and the
// refusals (the codes below).

#include "hikari/application/shape_presets.h"

#include <QObject>
#include <QRectF>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace hikari::ui {

class ShapeEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Opened by the visual tools")
    // The list: each preset's {name, path (its drawing as an SVG path),
    // box (its bounds), own (not one of legacy's defaults)}.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int selection READ selection WRITE select NOTIFY changed)
    // The selected preset.
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString shape READ shape NOTIFY changed)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QRectF box READ box NOTIFY changed)
    Q_PROPERTY(int nameMaxLength READ nameMaxLength CONSTANT)
    // The user's own presets (Restore default shapes removes them).
    Q_PROPERTY(int ownCount READ ownCount NOTIFY changed)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed)
    // Undo and Redo: whether there is a step, and which
    // (application::visual::ShapesEdition::Step; -1 none).
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int undoStep READ undoStep NOTIFY changed)
    Q_PROPERTY(int redoStep READ redoStep NOTIFY changed)
public:
    // NameCheck as QML reads it.
    enum NameCheck { NameOk, NameEmpty, NameTaken, NameSeparator };
    Q_ENUM(NameCheck)

    // A step's renames (Name, Replace, their Undo and Redo, Undo all): the
    // toolbar's chosen preset follows them.
    using Renames = application::visual::Renames;
    struct Hooks {
        std::function<std::u16string()> activeLineText; // New shape's and Take from the active line's Line
        std::function<std::vector<application::visual::ShapePreset>()> defaults;
        std::function<void(const std::vector<application::visual::ShapePreset> &, const Renames &)> changed;
        std::function<void()> finished;
    };
    ShapeEditor(std::vector<application::visual::ShapePreset> presets, int curShape, Hooks hooks,
                QObject *parent = nullptr);

    QVariantList items() const;
    int selection() const { return m_edition.selection(); }
    Q_INVOKABLE void select(int index);
    QString name() const;
    QString shape() const;
    int mode() const { return m_edition.current().mode; }
    void setMode(int mode);
    QString path() const;
    QRectF box() const;
    int nameMaxLength() const { return application::visual::ShapesEdition::kNameMaxLength; }
    int ownCount() const;
    bool canRemove() const { return m_edition.canRemove(); }
    bool canUndo() const { return m_edition.canUndo(); }
    bool canRedo() const { return m_edition.canRedo(); }
    int undoStep() const { return m_edition.undoStep(); }
    int redoStep() const { return m_edition.redoStep(); }

    // A name typed: NameCheck; with NameTaken `takenName()` has the other's.
    Q_INVOKABLE int setName(const QString &name);
    Q_INVOKABLE QString takenName() const { return m_taken; }
    // The drawing typed: false when the tool cannot use it.
    Q_INVOKABLE bool setShape(const QString &shape);
    Q_INVOKABLE void endTyping() { m_edition.endTyping(); }
    Q_INVOKABLE void addShape();
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE bool remove();
    // False when the active Line has no drawing.
    Q_INVOKABLE bool takeFromLine();
    Q_INVOKABLE void replaceNamed(const QString &name);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE bool undoAll();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // The window closed: the changes are kept already.
    Q_INVOKABLE void close();

    const application::visual::ShapesEdition &edition() const { return m_edition; }

    // An ASS drawing as an SVG path (m and n move, l draws lines, b a cubic
    // Bézier, s and p a B-spline as the renderer draws it, c closes) and its
    // bounds.
    static QString svgPath(std::u16string_view drawing, QRectF *bounds = nullptr);

signals:
    void changed();
    void closed();

private:
    void kept(); // a step: tell QML and hand the presets back
    application::visual::ShapesEdition m_edition;
    Hooks m_hooks;
    std::uint64_t m_published = 0; // the history's revision last handed on
    QString m_taken;
    bool m_done = false;
};

} // namespace hikari::ui
