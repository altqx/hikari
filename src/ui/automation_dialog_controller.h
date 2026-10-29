#pragma once

#include "hikari/application/automation.h"
#include "hikari/application/dialog_memory.h"

#include "script_dialog_memory.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace hikari::ui {

class AutomationDialogController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QVariantList controls READ controls NOTIFY changed)
    Q_PROPERTY(QStringList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(int columns READ columns NOTIFY changed)
    Q_PROPERTY(int enterButton READ enterButton NOTIFY changed)
    Q_PROPERTY(QString dialogId READ dialogId NOTIFY changed)
    Q_PROPERTY(bool remembers READ remembers NOTIFY changed)
    Q_PROPERTY(bool hasLastUsed READ hasLastUsed NOTIFY memoryChanged)
    Q_PROPERTY(QStringList presets READ presets NOTIFY memoryChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
public:
    using Reply = std::function<void(application::DialogResult)>;

    struct Shown {
        std::optional<application::DialogResult> values;
        QString memoryKey;
        QString notice;
    };

    explicit AutomationDialogController(QObject *parent = nullptr);

    void setMemory(ScriptDialogMemory *memory) { m_memory = memory; }

    void present(const QString &title, const application::DialogRequest &request, Reply reply,
                 const QString &source = QString(), Shown shown = {});
    void withdraw();

    bool isOpen() const { return static_cast<bool>(m_reply); }
    QString title() const { return m_title; }
    QVariantList controls() const { return m_controls; }
    QStringList buttons() const { return m_buttons; }
    int columns() const { return m_columns; }
    int enterButton() const { return m_enterButton; }
    QString dialogId() const { return m_dialogId; }
    bool remembers() const { return m_memory && !m_memoryKey.isEmpty(); }
    bool hasLastUsed() const;
    QStringList presets() const;
    QString notice() const { return m_notice; }

    Q_INVOKABLE QVariantList lastUsedValues() const;
    Q_INVOKABLE QVariantList presetValues(const QString &name) const;
    Q_INVOKABLE QVariantList scriptValues() const;
    Q_INVOKABLE bool differs(int index, const QVariant &value) const;
    Q_INVOKABLE QString scriptValueText(int index) const;
    Q_INVOKABLE void savePreset(const QString &name, const QVariantList &values);
    Q_INVOKABLE void deletePreset(const QString &name);

    Q_INVOKABLE void finish(int pressed, const QVariantList &values);

signals:
    void changed();
    void memoryChanged();

private:
    QVariantList shownValues(const application::NamedDialogValues &values) const;
    application::DialogResult resultOf(int pressed, const QVariantList &values) const;

    application::DialogRequest m_request;
    application::DialogResult m_shown;
    ScriptDialogMemory *m_memory = nullptr;
    QString m_memoryKey;
    QString m_notice;
    Reply m_reply;
    QString m_title;
    QVariantList m_controls;
    QStringList m_buttons;
    int m_columns = 1;
    int m_enterButton = 0;
    QString m_dialogId;
};

}
