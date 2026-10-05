#pragma once

// E6: the Line editor's tag list popup (legacy TextEditorTagList and the
// editor's tag-list keys at 20d647c4; core::taglist holds the rules). The
// field keeps keyboard focus and input-method composition throughout: the
// popup is drawn beside it and never takes focus. The options are
// TEXT_EDITOR_TAG_LIST_OPTIONS (textEditor.tagListOptions), read each time
// a list opens, as legacy's PopupTagList constructor reads them.

#include "hikari/core/tag_list.h"

#include <QFont>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace hikari::ui {

class TagListController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the Line editor")
    Q_PROPERTY(bool open READ open NOTIFY changed)
    // The popup exists: a list with at least one shown entry.
    Q_PROPERTY(bool popupShown READ popupShown NOTIFY changed)
    // The shown entries' row texts (GetTagText: the tag, " - " and the
    // description with "Show description").
    Q_PROPERTY(QStringList rows READ rows NOTIFY changed)
    Q_PROPERTY(int selection READ selection NOTIFY changed)
    Q_PROPERTY(int scrollPosition READ scrollPosition NOTIFY changed)
    // The options as last read or toggled.
    Q_PROPERTY(int options READ options NOTIFY changed)
    Q_PROPERTY(int maxVisible READ maxVisible CONSTANT)
    // What assistive technology hears for the selected row: "\fs, Font size,
    // 11 of 16".
    Q_PROPERTY(QString selectedAnnouncement READ selectedAnnouncement NOTIFY changed)
public:
    explicit TagListController(QObject *parent = nullptr);

    using ReadOptions = std::function<int()>;
    using WriteOptions = std::function<void(int)>;
    void setOptionsStore(ReadOptions read, WriteOptions write);

    bool open() const { return m_completion.open(); }
    bool popupShown() const { return m_completion.list() && m_completion.list()->popupShown(); }
    QStringList rows() const;
    int selection() const { return m_completion.list() ? m_completion.list()->selection() : -1; }
    int scrollPosition() const { return m_completion.list() ? m_completion.list()->scrollPosition() : 0; }
    int options() const { return m_options; }
    int maxVisible() const { return core::taglist::kMaxVisible; }
    QString selectedAnnouncement() const;

    // A character was typed into the field: its text and the caret after it.
    Q_INVOKABLE bool typed(const QString &text, int caret, const QString &key);
    // Up (-1) or Down (1); false when the list is closed.
    Q_INVOKABLE bool move(int delta);
    // Enter or a click: {text, caret} to put in the field, or an empty map.
    Q_INVOKABLE QVariantMap put(const QString &text, int caret);
    Q_INVOKABLE void close();
    // The mouse over the popup (OnMouseEvent): `row` from its top. True
    // when a click there acts.
    Q_INVOKABLE bool pointerAt(int row);
    Q_INVOKABLE void scrollBy(int rows);
    // The mouse wheel over the popup: `notches` up (positive) or down.
    Q_INVOKABLE void wheel(int notches);
    // The right-click menu: "Show description" (4), "Show all tags" (1),
    // "Show VSFiltermod tags" (2) flip in the stored options and the list
    // is filtered by them alone.
    Q_INVOKABLE void toggleOption(int bit);
    // CalcPosAndSize (TextEditorTagList.cpp:70-96) in `font`: the widest
    // row text of all 77 entries plus 18, 20 more with a scroll bar, at
    // most 800; once remade, at least the control's 100.
    Q_INVOKABLE double popupWidth(const QFont &font) const;
    // A row's height: the text height plus 6.
    Q_INVOKABLE double rowHeight(const QFont &font) const;

    // The translated description of an entry (legacy _() strings).
    static QString description(std::size_t entry);
    const core::taglist::Completion &completion() const { return m_completion; }

signals:
    void changed();

private:
    QString rowText(std::size_t entry) const;
    core::taglist::Completion m_completion;
    ReadOptions m_read;
    WriteOptions m_write;
    int m_options = 0;
};

} // namespace hikari::ui
