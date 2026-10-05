#pragma once

// K2: the theme colours the HikariStyle controls draw with that the
// application palette has no role for. The theme layer (src/ui/theme.cpp)
// sets them with the palette; until it does (no profile) the controls keep
// Fusion's own colours.
//
// focus: the theme's keyboard focus role (visual-language.md, `focus`). The
// controls draw their keyboard focus (visualFocus) in it, apart from the
// accent that marks selection, the default button and a focused field's
// border (the K2 card: "the accent drives ... focused field border").

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
public:
    explicit StyleColours(QObject *parent = nullptr);
    ~StyleColours() override;
    static StyleColours *create(QQmlEngine *engine, QJSEngine *js);

    // Whether the theme layer set the colours (else Fusion's stay).
    bool themed() const;
    QColor focus() const;

    // The theme layer's: an invalid colour returns the controls to Fusion's.
    static void setFocus(const QColor &focus);

signals:
    void changed();
};

} // namespace hikari::ui::style
