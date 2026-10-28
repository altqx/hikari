#pragma once

// T5: the "Vector shape editing" dialog's model for QML (legacy ShapesEdition,
// HikariSub/VisualDrawingShapes.cpp:43-288): a copy of the drawing tool's
// shape presets, edited through application::visual::ShapesEdition. The
// VisualToolsController opens it from the shape list's "Edit" and takes the
// presets back on OK.

#include "hikari/application/shape_presets.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace hikari::ui {

class ShapeEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Opened by the visual tools")
    // The dialog's list of presets (names as legacy's choice holds them).
    Q_PROPERTY(QStringList list READ list NOTIFY changed)
    Q_PROPERTY(int selection READ selection NOTIFY changed)
    // The fields.
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString shape READ shape WRITE setShape NOTIFY changed)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(int scalingMode READ scalingMode WRITE setScalingMode NOTIFY changed)
    Q_PROPERTY(int nameMaxLength READ nameMaxLength CONSTANT)
public:
    struct Hooks {
        std::function<std::u16string()> activeLineText; // OnGetShapeFromLine's Line
        std::function<void()> removeFile;               // OnResetDefault's _wremove
        std::function<std::vector<application::visual::ShapePreset>()> defaults; // LoadSettings without the file
        std::function<void(std::optional<std::vector<application::visual::ShapePreset>>)> finished;
    };
    ShapeEditor(std::vector<application::visual::ShapePreset> presets, int curShape, Hooks hooks,
                QObject *parent = nullptr);

    QStringList list() const;
    int selection() const { return m_edition.selection(); }
    QString name() const;
    QString shape() const;
    int mode() const { return m_edition.mode; }
    int scalingMode() const { return m_edition.scalingMode; }
    int nameMaxLength() const { return application::visual::ShapesEdition::kNameMaxLength; }
    void setName(const QString &name);
    void setShape(const QString &shape);
    void setMode(int mode);
    void setScalingMode(int mode);

    // Each returns {} or a message box to show: {text, title}.
    Q_INVOKABLE QVariantMap addShape(const QString &newName);
    Q_INVOKABLE QVariantMap removeShape();
    // OnListChanged: the question to ask first ({} when nothing changed).
    Q_INVOKABLE QVariantMap listChangeQuestion() const;
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE bool getShapeFromLine();
    // Apply (ok false) or OK: {} when kept (OK then closes), {text, title}
    // for an error, or {clash: name} when another preset has the name.
    Q_INVOKABLE QVariantMap save(bool ok);
    // The answer Replace to a clash: kept; `pending` (a list index the
    // dialog still moves to, or -1) comes back adjusted. With ok the dialog
    // closes.
    Q_INVOKABLE int replaceClash(int pending, bool ok);
    Q_INVOKABLE QVariantMap restoreQuestion() const;
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE void cancel();

    const application::visual::ShapesEdition &edition() const { return m_edition; }

signals:
    void changed();
    void closed();

private:
    void finish(bool ok);
    application::visual::ShapesEdition m_edition;
    Hooks m_hooks;
    bool m_done = false;
};

} // namespace hikari::ui
