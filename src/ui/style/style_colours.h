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
    Q_PROPERTY(QColor focus READ focus NOTIFY changed FINAL)
    Q_PROPERTY(QColor control READ control NOTIFY changed FINAL)
public:
    explicit StyleColours(QObject *parent = nullptr);
    ~StyleColours() override;
    static StyleColours *create(QQmlEngine *engine, QJSEngine *js);

    bool themed() const;
    QColor focus() const;
    QColor control() const;

    static void setFocus(const QColor &focus);
    static void setControl(const QColor &control);

signals:
    void changed();
};

}
