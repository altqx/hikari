#pragma once

// T6: the "Tag editing" dialog's model for QML (legacy AllTagsEdition,
// HikariSub/VisualAllTagsEdition.cpp:155-455): a copy of the all-tags tool's
// definitions, edited through application::visual::AllTagsEdition. The
// VisualToolsController opens it from the tool row's Edit and takes the
// definitions back on OK.

#include "hikari/application/all_tags.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace hikari::ui {

class AllTagsEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Opened by the visual tools")
    // The dialog's list (names as legacy's choice holds them).
    Q_PROPERTY(QStringList list READ list NOTIFY changed)
    Q_PROPERTY(int selection READ selection NOTIFY changed)
    // The fields: the texts, the number fields' texts as typed or set
    // (`numbers`: minimum, maximum, value, step, decimal places, values 3-5
    // as legacy's tooltips number them), the three choices.
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY changed)
    Q_PROPERTY(QVariantMap numbers READ numbers NOTIFY changed)
    Q_PROPERTY(int placing READ placing WRITE setPlacing NOTIFY changed)
    Q_PROPERTY(int additionalValues READ additionalValues WRITE setAdditionalValues NOTIFY changed)
    Q_PROPERTY(int changeOption READ changeOption WRITE setChangeOption NOTIFY changed)
    Q_PROPERTY(QStringList placings READ placings CONSTANT)
    Q_PROPERTY(QStringList valueCounts READ valueCounts CONSTANT)
    Q_PROPERTY(QStringList changeOptions READ changeOptions CONSTANT)
public:
    struct Hooks {
        std::function<void()> removeFile; // OnResetDefault's _wremove
        std::function<void(std::optional<std::vector<application::visual::AllTagsSetting>>)> finished;
    };
    AllTagsEditor(std::vector<application::visual::AllTagsSetting> tags, int curTag, Hooks hooks,
                  QObject *parent = nullptr);

    QStringList list() const;
    int selection() const { return m_edition.selection(); }
    QString name() const;
    QString tag() const;
    QVariantMap numbers() const;
    int placing() const { return m_edition.placing; }
    int additionalValues() const { return m_edition.additionalValues; }
    int changeOption() const { return m_edition.changeOption; }
    QStringList placings() const;
    QStringList valueCounts() const;
    QStringList changeOptions() const;
    void setName(const QString &name);
    void setTag(const QString &tag);
    void setPlacing(int placing);
    void setAdditionalValues(int count);
    void setChangeOption(int option);

    // A number field typed into: "min", "max", "value", "step", "digits",
    // "value2", "value3" or "value4".
    Q_INVOKABLE void setNumber(const QString &field, const QString &text);
    // Each returns {} or a message box to show: {text, title}.
    Q_INVOKABLE QVariantMap addTag(const QString &newName);
    Q_INVOKABLE QVariantMap removeTag();
    // OnListChanged: the question to ask first ({} when nothing changed).
    Q_INVOKABLE QVariantMap listChangeQuestion() const;
    Q_INVOKABLE void select(int index);
    // Apply (ok false) or OK: {} when kept (OK then closes), or {text,
    // title} for an error.
    Q_INVOKABLE QVariantMap save(bool ok);
    Q_INVOKABLE QVariantMap restoreQuestion() const;
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE void cancel();

    const application::visual::AllTagsEdition &edition() const { return m_edition; }

signals:
    void changed();
    void closed();

private:
    application::visual::NumberField *field(const QString &name);
    void finish(bool ok);
    application::visual::AllTagsEdition m_edition;
    Hooks m_hooks;
    bool m_done = false;
};

} // namespace hikari::ui
