#pragma once

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
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY changed FINAL)
    Q_PROPERTY(QColor focus READ focus NOTIFY changed FINAL)
    Q_PROPERTY(QColor control READ control NOTIFY changed FINAL)
    Q_PROPERTY(QColor text READ text NOTIFY changed FINAL)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed FINAL)
    Q_PROPERTY(QColor line READ line NOTIFY changed FINAL)
    Q_PROPERTY(QColor panel READ panel NOTIFY changed FINAL)
    Q_PROPERTY(QColor raised READ raised NOTIFY changed FINAL)
    Q_PROPERTY(QColor field READ field NOTIFY changed FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed FINAL)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed FINAL)
    Q_PROPERTY(QColor select READ select NOTIFY changed FINAL)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed FINAL)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed FINAL)
    Q_PROPERTY(QColor disabled READ disabled NOTIFY changed FINAL)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed FINAL)
public:
    struct Colours {
        QColor focus, control, text, muted, line, panel, raised, field, accent, onAccent, select, hover, pressed, disabled, danger;
        bool highContrast = false;
        bool operator==(const Colours &) const = default;
    };

    explicit StyleColours(QObject *parent = nullptr);
    ~StyleColours() override;
    static StyleColours *create(QQmlEngine *engine, QJSEngine *js);

    bool themed() const;
    bool highContrast() const;
    QColor focus() const;
    QColor control() const;
    QColor text() const;
    QColor muted() const;
    QColor line() const;
    QColor panel() const;
    QColor raised() const;
    QColor field() const;
    QColor accent() const;
    QColor onAccent() const;
    QColor select() const;
    QColor hover() const;
    QColor pressed() const;
    QColor disabled() const;
    QColor danger() const;

    static void set(const Colours &colours);

signals:
    void changed();
};

}
