#pragma once

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
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(QStringList iconRoles READ iconRoles CONSTANT)
    Q_PROPERTY(int maximum READ maximum CONSTANT)
public:
    static constexpr int kMaximum = 20;
    struct Button {
        QString tag;
        // MUST KILL: type is a magic int (0 at the caret, 1 at the text start, 2 plain text)
        int type = 0;
        QString name;
        QString icon;
    };

    explicit TagButtonsController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit TagButtonsController(SettingsStore &settings, QObject *parent = nullptr);

    int count() const { return m_count; }
    QVariantList buttons() const;
    static QStringList iconRoles();
    static int maximum() { return kMaximum; }
    const Button &button(int index) const { return m_buttons[static_cast<std::size_t>(index)]; }

    Q_INVOKABLE int storedCount() const;
    Q_INVOKABLE void setCount(int count);
    Q_INVOKABLE void edit(int index, const QString &name, const QString &tag, int type, const QString &icon = {});
    Q_INVOKABLE bool add(const QString &name, const QString &tag, int type, const QString &icon = {});
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE bool move(int index, int step);
    Q_INVOKABLE void replace(const QVariantList &buttons);
    Q_INVOKABLE QString problem(const QString &tag, int type) const;
    // MUST KILL: pressed() re-reads the stored option, dropping empty entries, instead of the shown button
    Q_INVOKABLE QVariantMap pressed(int index) const;

    // MUST KILL: options are stored as legacy's "{\n\t<tag>\n\t<type>\n\t<name>[\n\t<icon>]\n}" text
    static Button fromLegacy(const QString &value, int index);
    static QString toLegacy(const Button &button);

    void reload();

signals:
    void changed();

private:
    void load();
    void store(int index);
    void storeCount();

    std::unique_ptr<SettingsStore> m_ownedSettings;
    SettingsStore *m_settings;
    int m_count = 0;
    std::array<Button, kMaximum> m_buttons;
};

}
