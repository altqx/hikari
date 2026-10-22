#pragma once

// PU5 (#307): the "Edit tags" window's model for QML (legacy AllTagsEdition,
// "Tag editing", HikariSub/VisualAllTagsEdition.cpp:155-455, redesigned):
// the all-tags tool's definitions, edited through
// application::visual::AllTagsEdition. The VisualToolsController opens it
// from Hydra's "Edit tags…" and takes the definitions back after every
// change (hooks.changed: the tool's list and the file), until the window
// closes (hooks.finished). The window words the steps and the refusals
// (the codes below) and the change options in the Hydra strip's words.

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
    // The list: each definition's {name, tag, section (0 Common, 1 More, 2
    // the user's own)}, in the file's order.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int selection READ selection WRITE select NOTIFY changed)
    // The selected definition.
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString tag READ tag NOTIFY changed)
    // The number fields' texts: "min", "max", "step", "digits", "value0" to
    // "value3".
    Q_PROPERTY(QVariantMap numbers READ numbers NOTIFY changed)
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY changed)
    // The change option it brings (AllTagsPasteMode) and Set's placing.
    Q_PROPERTY(int changeOption READ changeOption WRITE setChangeOption NOTIFY changed)
    Q_PROPERTY(int placing READ placing WRITE setPlacing NOTIFY changed)
    // Its values' names in the strip (as many as it takes).
    Q_PROPERTY(QStringList valueNames READ valueNames NOTIFY changed)
    // The preview of Set on the active Line: {before, tag, after}, the tag
    // the part Set writes.
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY changed)
    // The user's own definitions (Restore default tags removes them).
    Q_PROPERTY(int ownCount READ ownCount NOTIFY changed)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY changed)
    // Undo and Redo: whether there is a step, and which
    // (application::visual::AllTagsEdition::Step; -1 none).
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int undoStep READ undoStep NOTIFY changed)
    Q_PROPERTY(int redoStep READ redoStep NOTIFY changed)
public:
    // AllTagsEdition::Check as QML reads it.
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
        CheckStepTooLarge
    };
    Q_ENUM(Check)

    struct Hooks {
        std::function<std::u16string()> activeLineText; // the preview's Line
        std::function<void(const std::vector<application::visual::AllTagsSetting> &)> changed;
        std::function<void()> finished;
    };
    AllTagsEditor(std::vector<application::visual::AllTagsSetting> tags, int curTag, Hooks hooks,
                  QObject *parent = nullptr);

    QVariantList items() const;
    int selection() const { return m_edition.selection(); }
    void select(int index);
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

    // Typed into a field: Check; with CheckTaken `takenName()` has the
    // other's name, with a range check `limit()` the other end or the
    // largest step.
    Q_INVOKABLE int setName(const QString &name);
    Q_INVOKABLE int setTag(const QString &tag);
    // "min", "max", "step", "digits", "value0" to "value3".
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
    // The window closed: the changes are kept already.
    Q_INVOKABLE void close();

    const application::visual::AllTagsEdition &edition() const { return m_edition; }

signals:
    void changed();
    void closed();

private:
    void kept(); // a step: tell QML and hand the definitions back
    int checked(application::visual::AllTagsEdition::Check check);
    application::visual::AllTagsEdition m_edition;
    Hooks m_hooks;
    QString m_taken;
    QString m_limit;
    bool m_done = false;
};

} // namespace hikari::ui
