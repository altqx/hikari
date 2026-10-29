#pragma once

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
    Q_PROPERTY(bool popupShown READ popupShown NOTIFY changed)
    Q_PROPERTY(QStringList rows READ rows NOTIFY changed)
    Q_PROPERTY(int selection READ selection NOTIFY changed)
    Q_PROPERTY(int scrollPosition READ scrollPosition NOTIFY changed)
    Q_PROPERTY(int options READ options NOTIFY changed)
    Q_PROPERTY(int maxVisible READ maxVisible CONSTANT)
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

    Q_INVOKABLE bool typed(const QString &text, int caret, const QString &key);
    Q_INVOKABLE bool move(int delta);
    Q_INVOKABLE QVariantMap put(const QString &text, int caret);
    Q_INVOKABLE void close();
    Q_INVOKABLE bool pointerAt(int row);
    Q_INVOKABLE void scrollBy(int rows);
    Q_INVOKABLE void wheel(int notches);
    // MUST KILL: toggleOption takes raw option bits (4 description, 1 all tags, 2 VSFiltermod)
    Q_INVOKABLE void toggleOption(int bit);
    Q_INVOKABLE double popupWidth(const QFont &font) const;
    Q_INVOKABLE double rowHeight(const QFont &font) const;

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

}
