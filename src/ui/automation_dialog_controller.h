#pragma once

// Presents a script's dialog (N8; docs/qt/automation.md) with fixed QML
// controls built from plain data, and returns typed values. Nothing the
// script supplies becomes QML. Each presentation is answered exactly once:
// by a button, by closing the window, or not at all when withdrawn (the run
// ended first).

#include "hikari/application/automation.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace hikari::ui {

class AutomationDialogController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    // One map per control: kind, name, hint, x, y, width, height, label, text,
    // intValue, intMin, intMax, number, numberMin, numberMax, checked, items.
    Q_PROPERTY(QVariantList controls READ controls NOTIFY changed)
    // Labels to show; the default pair appears as OK and Cancel.
    Q_PROPERTY(QStringList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(int columns READ columns NOTIFY changed)
public:
    using Reply = std::function<void(application::DialogResult)>;

    explicit AutomationDialogController(QObject *parent = nullptr);

    void present(const QString &title, const application::DialogRequest &request, Reply reply);
    void withdraw();

    bool isOpen() const { return static_cast<bool>(m_reply); }
    QString title() const { return m_title; }
    QVariantList controls() const { return m_controls; }
    QStringList buttons() const { return m_buttons; }
    int columns() const { return m_columns; }

    // From QML: `pressed` is a button index, or -1 when the window was closed.
    // `values` holds each control's current value in order; each is coerced
    // to its control's type (integers and numbers clamped to their range).
    Q_INVOKABLE void finish(int pressed, const QVariantList &values);

signals:
    void changed();

private:
    application::DialogRequest m_request;
    Reply m_reply;
    QString m_title;
    QVariantList m_controls;
    QStringList m_buttons;
    int m_columns = 1;
};

} // namespace hikari::ui
