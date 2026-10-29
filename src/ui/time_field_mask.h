#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class TimeFieldMask : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantMap type(int form, const QString &text, int start, int end, const QString &character) const;
    Q_INVOKABLE QVariantMap backspace(const QString &text, int start, int end) const;
    Q_INVOKABLE QVariantMap paste(int form, const QString &text, int start, int end) const;
    Q_INVOKABLE void copy(const QString &text) const;
    Q_INVOKABLE bool holds(int form, const QString &text) const;
    // MUST KILL: mouseForm is a magic int (0 ASS, 1 SRT, 2 TMPlayer, 3 MicroDVD, 4 MPL2); form is application::TimeFieldForm as an int
    Q_INVOKABLE QString wheel(int mouseForm, bool frames, const QString &text, int column, int notches) const;
    Q_INVOKABLE QVariantMap dragBegin(int mouseForm, bool frames, const QString &text, int column, int x,
                                      int y) const;
    Q_INVOKABLE QVariantMap dragMove(int mouseForm, bool frames, const QVariantMap &drag, int x, int y) const;
};

}
