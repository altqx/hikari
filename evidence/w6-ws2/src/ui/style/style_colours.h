#pragma once

// K2: the theme colours the HikariStyle controls draw with that the
// application palette has no role for. The theme layer (src/ui/theme.cpp)
// sets them with the palette; until it does (no profile) the controls keep
// Fusion's own colours.
//
// focus: the theme's keyboard focus role (visual-language.md, `focus`: the
// text colour, MuseScore's convention). The controls draw their keyboard
// focus ring (FocusFrame.qml) in it, apart from the accent that marks
// selection, the default button and a focused field's border (the K2 card:
// "the accent drives ... focused field border").
//
// control: an enabled control's boundary (visual-language.md, `control`):
// at least 3:1 against the theme's surfaces in every theme (WCAG 1.4.11,
// K2-control-boundary), the high-contrast border pick in those themes. The
// controls a user operates draw their outline in it; frames, group boxes,
// menus, tab bars and disabled controls keep the palette's Mid, `line`.

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace hikari::ui::style {

class StyleColours : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool themed READ themed NOTIFY changed FINAL)
    Q_PROPERTY(QColor focus READ focus NOTIFY changed FINAL)
    Q_PROPERTY(QColor control READ control NOTIFY changed FINAL)
public:
    explicit StyleColours(QObject *parent = nullptr);
    ~StyleColours() override;
    static StyleColours *create(QQmlEngine *engine, QJSEngine *js);

    // Whether the theme layer set the colours (else Fusion's stay).
    bool themed() const;
    QColor focus() const;
    QColor control() const;

    // The theme layer's: an invalid colour returns the controls to Fusion's.
    static void setFocus(const QColor &focus);
    static void setControl(const QColor &control);

signals:
    void changed();
};

} // namespace hikari::ui::style
