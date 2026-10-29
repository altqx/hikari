#pragma once

#include "hikari/application/all_tags.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace hikari::ui {

class AllTagsEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Opened by the visual tools")
    // MUST KILL: items' section is a magic int (0 Common, 1 More, 2 the user's own)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int selection READ selection WRITE select NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString tag READ tag NOTIFY changed)
    Q_PROPERTY(QVariantMap numbers READ numbers NOTIFY changed)
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY changed)
    // MUST KILL: changeOption is tagMode (AllTagsPasteMode) and placing is mode, both as magic ints
    Q_PROPERTY(int changeOption READ changeOption WRITE setChangeOption NOTIFY changed)
    Q_PROPERTY(int placing READ placing WRITE setPlacing NOTIFY changed)
    Q_PROPERTY(QStringList valueNames READ valueNames NOTIFY changed)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(int ownCount READ ownCount NOTIFY changed)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    // MUST KILL: undoStep and redoStep are AllTagsEdition::Step as ints, -1 for none
    Q_PROPERTY(int undoStep READ undoStep NOTIFY changed)
    Q_PROPERTY(int redoStep READ redoStep NOTIFY changed)
public:
    // MUST KILL: Check mirrors AllTagsEdition::Check by order; checked() casts one to the other
    enum Check {
        CheckOk,
        CheckEmpty,
        CheckTaken,
        CheckSeparator,
        CheckNotANumber,
        CheckOutOfRange,
        CheckMaxNotAboveMin,
        CheckMinNotBelowMax,
        CheckStepNotPositive,
        CheckStepTooLarge,
        CheckTakenLastUsable
    };
    Q_ENUM(Check)

    using Renames = application::visual::Renames;
    struct Hooks {
        std::function<std::u16string()> activeLineText;
        std::function<void(const std::vector<application::visual::AllTagsSetting> &, const Renames &)> changed;
        std::function<void()> finished;
    };
    AllTagsEditor(std::vector<application::visual::AllTagsSetting> tags, int curTag, Hooks hooks,
                  QObject *parent = nullptr);

    QVariantList items() const;
    int selection() const { return m_edition.selection(); }
    Q_INVOKABLE void select(int index);
    QString name() const;
    QString tag() const;
    QVariantMap numbers() const;
    int count() const { return m_edition.current().numOfValues; }
    void setCount(int count);
    int changeOption() const { return m_edition.current().tagMode; }
    void setChangeOption(int option);
    int placing() const { return m_edition.current().mode; }
    void setPlacing(int placing);
    QStringList valueNames() const;
    QVariantMap preview() const;
    int ownCount() const;
    bool canRemove() const { return m_edition.canRemove(); }
    bool canUndo() const { return m_edition.canUndo(); }
    bool canRedo() const { return m_edition.canRedo(); }
    int undoStep() const { return m_edition.undoStep(); }
    int redoStep() const { return m_edition.redoStep(); }

    // MUST KILL: the setters return a Check as int and leave takenName() and limit() as side results
    Q_INVOKABLE int setName(const QString &name);
    Q_INVOKABLE int setTag(const QString &tag);
    Q_INVOKABLE int setNumber(const QString &field, const QString &text);
    Q_INVOKABLE QString takenName() const { return m_taken; }
    Q_INVOKABLE QString limit() const { return m_limit; }
    Q_INVOKABLE void endTyping() { m_edition.endTyping(); }
    Q_INVOKABLE void addTag();
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE bool remove();
    Q_INVOKABLE void replaceNamed(const QString &name);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE bool undoAll();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    void setPlacingOf(const QString &name, int placing);
    Q_INVOKABLE void close();
    void lineChanged() { emit previewChanged(); }

    const application::visual::AllTagsEdition &edition() const { return m_edition; }

signals:
    void changed();
    void previewChanged();
    void closed();

private:
    void kept();
    int checked(application::visual::AllTagsEdition::Check check);
    application::visual::AllTagsEdition m_edition;
    Hooks m_hooks;
    std::uint64_t m_published = 0;
    QString m_taken;
    QString m_limit;
    bool m_done = false;
};

}
