#pragma once

// Presents a script's dialog (N8; docs/automation.md) with fixed QML
// controls built from plain data, and returns typed values. Nothing the
// script supplies becomes QML. Each presentation is answered exactly once:
// by a button, by closing the window, or not at all when withdrawn (the run
// ended first).

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
    // One map per control: kind, name, hint, x, y, width, height, label, text,
    // intValue, intMin, intMax, number, numberMin, numberMax, step, checked,
    // items.
    Q_PROPERTY(QVariantList controls READ controls NOTIFY changed)
    // Labels to show; the default pair appears as OK and Cancel.
    Q_PROPERTY(QStringList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(int columns READ columns NOTIFY changed)
    // AD1: the button Enter presses: Aegisub's affirmative one (an ok, yes
    // or save id; the last such, as each makes itself wx's default), else
    // the first.
    Q_PROPERTY(int enterButton READ enterButton NOTIFY changed)
    // AD1: the id the dialog's size and place are kept under
    // (DialogPlacement): one per script file, macro and set of controls
    // (their classes and names), so one script's dialogs keep their own.
    Q_PROPERTY(QString dialogId READ dialogId NOTIFY changed)
    // AD2 (#311): the ⋯ menu. Whether the dialog's answers and presets are
    // kept (it comes from a script), whether it has a last answer, and its
    // presets by name.
    Q_PROPERTY(bool remembers READ remembers NOTIFY changed)
    Q_PROPERTY(bool hasLastUsed READ hasLastUsed NOTIFY memoryChanged)
    Q_PROPERTY(QStringList presets READ presets NOTIFY memoryChanged)
    // AD2-repeat-last-run: why a repeated run shows the dialog (the inline
    // bar); empty for a dialog shown as usual.
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
public:
    using Reply = std::function<void(application::DialogResult)>;

    // AD2: how a dialog is shown besides the script's request.
    struct Shown {
        // The values shown, one per control; nullopt: the script's.
        std::optional<application::DialogResult> values;
        // The key its answers and presets are kept under
        // (ScriptDialogMemory::keyOf); empty: none are.
        QString memoryKey;
        QString notice;
    };

    explicit AutomationDialogController(QObject *parent = nullptr);

    // The kept answers and presets (null: none are kept).
    void setMemory(ScriptDialogMemory *memory) { m_memory = memory; }

    // `source` names where the dialog comes from (the script's file and
    // macro); it keys the kept size with the controls.
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

    // AD2: values to fill the dialog with, one per control as QML shows it
    // (undefined: the control is left as it is): the last answer, a preset,
    // and the script's values (Reset).
    Q_INVOKABLE QVariantList lastUsedValues() const;
    Q_INVOKABLE QVariantList presetValues(const QString &name) const;
    Q_INVOKABLE QVariantList scriptValues() const;
    // Whether control `index` showing `value` differs from the script's
    // value (the marker), and the script's value as the marker says it.
    Q_INVOKABLE bool differs(int index, const QVariant &value) const;
    Q_INVOKABLE QString scriptValueText(int index) const;
    // Saves the values shown (one per control, as finish() takes them) as
    // the preset `name`, in place of one of that name; deletes one.
    Q_INVOKABLE void savePreset(const QString &name, const QVariantList &values);
    Q_INVOKABLE void deletePreset(const QString &name);

    // From QML: `pressed` is a button index, or -1 when the window was closed.
    // `values` holds each control's current value in order; each is coerced
    // to its control's type (integers and numbers clamped to their range).
    Q_INVOKABLE void finish(int pressed, const QVariantList &values);

signals:
    void changed();
    void memoryChanged();

private:
    QVariantList shownValues(const application::NamedDialogValues &values) const;
    application::DialogResult resultOf(int pressed, const QVariantList &values) const;

    application::DialogRequest m_request;
    // The values shown when it opened (the script's, or filled in).
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

} // namespace hikari::ui
