#pragma once

// E4: TimeField.qml's masked editing (legacy TimeCtrl, HikariSub/TimeCtrl.cpp)
// through application::typeIntoTimeField, backspaceTimeField and
// pasteIntoTimeField. Each edit answers {text, selectionStart, selectionEnd,
// changed, bell} for the field to take; `form` is application::TimeFieldForm.

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
    // The clipboard's text, taken only when it is the form's (TimeCtrl's Ctrl+V).
    Q_INVOKABLE QVariantMap paste(int form, const QString &text, int start, int end) const;
    // TimeCtrl's Ctrl+C in a clock form: the whole time to the clipboard.
    Q_INVOKABLE void copy(const QString &text) const;
    // Whether the text is a whole value of the form (a number: digits, up to
    // application::kTimeFieldDigits, or empty).
    Q_INVOKABLE bool holds(int form, const QString &text) const;
    // TimeCtrl's mouse (application::wheelTimeField, beginTimeFieldDrag,
    // dragTimeField). `mouseForm` is application::TimeCtrlForm (0 ASS, 1 SRT,
    // 2 TMPlayer, 3 MicroDVD, 4 MPL2), `frames` whether the field shows
    // frames. The wheel's new text, or "" for none.
    Q_INVOKABLE QString wheel(int mouseForm, bool frames, const QString &text, int column, int notches) const;
    // A right-button drag: {value, step, x, y} for the field to keep; each
    // move answers {drag, text} (text "" for no step).
    Q_INVOKABLE QVariantMap dragBegin(int mouseForm, bool frames, const QString &text, int column, int x,
                                      int y) const;
    Q_INVOKABLE QVariantMap dragMove(int mouseForm, bool frames, const QVariantMap &drag, int x, int y) const;
};

} // namespace hikari::ui
