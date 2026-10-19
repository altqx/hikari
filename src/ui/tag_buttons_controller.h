#pragma once

// The Line editor's custom tag buttons (E2; legacy EDITBOX_TAG_BUTTONS and
// EDITBOX_TAG_BUTTON_VALUE1-20 at 20d647c4): how many are shown (0 to 20,
// none by default) and each button's tag, insertion type and name. Each
// definition is kept in the legacy option text, so a settings import can
// carry legacy values over unchanged. Kept in the settings registry
// (editor.tagButtons, editor.tagButton1-20).
//
// LE5 (#230): buttons are added by the strip's "+" (at the end, the count
// one more), edited, removed (the ones after it move up) and moved left or
// right from a button's context menu, and edited as a list in Settings >
// Editor. A button may have an icon of the set (iconRoles): a fourth entry in
// its option text, written only when it has one, so an option without an
// icon is the legacy text unchanged. EDITBOX_TAG_BUTTON1-20 stay the
// positions' bindings, as legacy's ids are.

#include "settings_store.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>

namespace hikari::ui {

class TagButtonsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(int count READ count NOTIFY changed)
    // {name, tag, type, icon} for each shown button; type 0 at the caret, 1
    // at the text start, 2 plain text; icon an icon role or "".
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
    // LE5: the icons a button may take, those of the override tags.
    Q_PROPERTY(QStringList iconRoles READ iconRoles CONSTANT)
    Q_PROPERTY(int maximum READ maximum CONSTANT)
public:
    static constexpr int kMaximum = 20;
    struct Button {
        QString tag;
        int type = 0;
        QString name;
        QString icon; // LE5: an icon role, or empty
    };

    // Settings kept in the INI file `settingsFile` (in memory when empty),
    // or in the application's store.
    explicit TagButtonsController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit TagButtonsController(SettingsStore &settings, QObject *parent = nullptr);

    int count() const { return m_count; }
    QVariantList buttons() const;
    static QStringList iconRoles();
    static int maximum() { return kMaximum; }
    const Button &button(int index) const { return m_buttons[static_cast<std::size_t>(index)]; }

    // "Change number of buttons": the stored count it starts from, and OK.
    Q_INVOKABLE int storedCount() const;
    Q_INVOKABLE void setCount(int count);
    // The TagButtonDialog's "Save tag"; LE5 the popover's and the Settings
    // list's Save, with the icon (one of iconRoles, else none).
    Q_INVOKABLE void edit(int index, const QString &name, const QString &tag, int type, const QString &icon = {});
    // LE5: the "+" popover's Save: a button after the shown ones; false at
    // the maximum.
    Q_INVOKABLE bool add(const QString &name, const QString &tag, int type, const QString &icon = {});
    // The context menu's Remove: the buttons after it move one place left.
    Q_INVOKABLE void remove(int index);
    // Move left (-1) or right (+1); false at the strip's end.
    Q_INVOKABLE bool move(int index, int step);
    // Settings > Editor's OK: the whole list ({name, tag, type, icon} each),
    // written only where it differs from the shown buttons.
    Q_INVOKABLE void replace(const QVariantList &buttons);
    // The popover's check of a tag text: "" when it can be saved, else what
    // is wrong (types 0 and 1 take override tags, \name and its value,
    // without the braces; plain text anything but nothing).
    Q_INVOKABLE QString problem(const QString &tag, int type) const;
    // Pressing a button or its menu entry (EditBox::OnButtonTag): its option
    // as it is stored now, {tag, type}, or nothing (legacy wxBell) when it
    // has fewer than two entries. The shown buttons are what SetTagButtons
    // built; after "Set default" they stay while their options are reset.
    Q_INVOKABLE QVariantMap pressed(int index) const;

    // config::GetTable with empty entries kept: "{\n\t<tag>\n\t<type>\n\t<name>\n}";
    // a missing name is "T<n>" (1-based), a missing type 0. LE5: a fourth
    // entry is the icon ("\n\t<icon>" before "\n}", only when there is one).
    static Button fromLegacy(const QString &value, int index);
    static QString toLegacy(const Button &button);

    // Reads the settings again.
    void reload();

signals:
    void changed();

private:
    void load();
    void store(int index); // the button's option
    void storeCount();

    std::unique_ptr<SettingsStore> m_ownedSettings;
    SettingsStore *m_settings;
    int m_count = 0;
    std::array<Button, kMaximum> m_buttons;
};

} // namespace hikari::ui
