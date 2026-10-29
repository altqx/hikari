#pragma once

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
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int selection READ selection WRITE select NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString shape READ shape NOTIFY changed)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QRectF box READ box NOTIFY changed)
    Q_PROPERTY(int nameMaxLength READ nameMaxLength CONSTANT)
    Q_PROPERTY(int ownCount READ ownCount NOTIFY changed)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    // MUST KILL: undoStep and redoStep are ShapesEdition::Step ints, -1 for none
    Q_PROPERTY(int undoStep READ undoStep NOTIFY changed)
    Q_PROPERTY(int redoStep READ redoStep NOTIFY changed)
public:
    enum NameCheck { NameOk, NameEmpty, NameTaken, NameSeparator };
    Q_ENUM(NameCheck)

    using Renames = application::visual::Renames;
    struct Hooks {
        std::function<std::u16string()> activeLineText;
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

    Q_INVOKABLE int setName(const QString &name);
    Q_INVOKABLE QString takenName() const { return m_taken; }
    Q_INVOKABLE bool setShape(const QString &shape);
    Q_INVOKABLE void endTyping() { m_edition.endTyping(); }
    Q_INVOKABLE void addShape();
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE bool remove();
    Q_INVOKABLE bool takeFromLine();
    Q_INVOKABLE void replaceNamed(const QString &name);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE bool undoAll();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void close();

    const application::visual::ShapesEdition &edition() const { return m_edition; }

    static QString svgPath(std::u16string_view drawing, QRectF *bounds = nullptr);

signals:
    void changed();
    void closed();

private:
    void kept();
    application::visual::ShapesEdition m_edition;
    Hooks m_hooks;
    std::uint64_t m_published = 0;
    QString m_taken;
    bool m_done = false;
};

}
