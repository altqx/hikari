#pragma once

// The Line editor's custom tag buttons (E2; legacy EDITBOX_TAG_BUTTONS and
// EDITBOX_TAG_BUTTON_VALUE1-20 at 20d647c4): how many are shown (0 to 20,
// none by default) and each button's tag, insertion type and name. Each
// definition is kept in the legacy option text, so a settings import can
// carry legacy values over unchanged. Kept in the settings registry
// (editor.tagButtons, editor.tagButton1-20).

#include "settings_store.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>

namespace hikari::ui {

class TagButtonsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(int count READ count NOTIFY changed)
    // {name, tag, type} for each shown button; type 0 at the caret, 1 at the
    // text start, 2 plain text.
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
public:
    static constexpr int kMaximum = 20;
    struct Button {
        QString tag;
        int type = 0;
        QString name;
    };

    // Settings kept in the INI file `settingsFile` (in memory when empty),
    // or in the application's store.
    explicit TagButtonsController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit TagButtonsController(SettingsStore &settings, QObject *parent = nullptr);

    int count() const { return m_count; }
    QVariantList buttons() const;
    const Button &button(int index) const { return m_buttons[static_cast<std::size_t>(index)]; }

    // "Change number of buttons".
    Q_INVOKABLE void setCount(int count);
    // The TagButtonDialog's "Save tag".
    Q_INVOKABLE void edit(int index, const QString &name, const QString &tag, int type);

    // config::GetTable with empty entries kept: "{\n\t<tag>\n\t<type>\n\t<name>\n}";
    // a missing name is "T<n>" (1-based), a missing type 0.
    static Button fromLegacy(const QString &value, int index);
    static QString toLegacy(const Button &button);

    // Reads the settings again (after "Set default").
    void reload();

signals:
    void changed();

private:
    void load();
    void save() const;

    std::unique_ptr<SettingsStore> m_ownedSettings;
    SettingsStore *m_settings;
    int m_count = 0;
    std::array<Button, kMaximum> m_buttons;
};

} // namespace hikari::ui
