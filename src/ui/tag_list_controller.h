#pragma once

#include "hikari/core/tag_list.h"

#include <QFont>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace hikari::ui {

// A plugin's completion, below the tag rows. Offsets are UTF-16, into the field's text.
struct ProviderRow {
    QString label;
    QString insert;
    QString detail;
    QString source;
    int from = 0;
    int to = 0;
    bool snippet = false;
};

struct ProviderHover {
    QString text;
    QString source;
    int from = 0;
    int to = 0;
};

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
    Q_PROPERTY(int tagRows READ tagRows NOTIFY changed)
    Q_PROPERTY(QStringList providerDetails READ providerDetails NOTIFY changed)
    // {role, at, text, source, from, to} of the last hover a plugin gave; empty without one.
    Q_PROPERTY(QVariantMap hover READ hover NOTIFY hoverChanged)
public:
    explicit TagListController(QObject *parent = nullptr);

    using ReadOptions = std::function<int()>;
    using WriteOptions = std::function<void(int)>;
    void setOptionsStore(ReadOptions read, WriteOptions write);
    using Rows = std::function<void(std::vector<ProviderRow>)>;
    // role 0 is the text, 1 the translation; trigger is empty for Ctrl+Space.
    using AskProviders = std::function<void(int role, const QString &text, int caret, const QString &trigger, Rows answer)>;
    using Triggers = std::function<QStringList(int role)>;
    void setProviders(Triggers triggers, AskProviders ask);
    using HoverAnswer = std::function<void(std::optional<ProviderHover>)>;
    using AskHover = std::function<void(int role, const QString &text, int position, HoverAnswer answer)>;
    using Hovers = std::function<bool(int role)>;
    void setHover(Hovers hovers, AskHover ask);

    bool open() const { return m_completion.open() || !m_provided.empty(); }
    bool popupShown() const { return tagRows() > 0 || !m_provided.empty(); }
    QStringList rows() const;
    int selection() const;
    int tagRows() const;
    QStringList providerDetails() const;
    int scrollPosition() const;
    int options() const { return m_options; }
    int maxVisible() const { return core::taglist::kMaxVisible; }
    QString selectedAnnouncement() const;

    Q_INVOKABLE bool typed(const QString &text, int caret, const QString &key, int role = 0);
    // Ctrl+Space: the tag rows when the word before the caret is a tag, and every provider's rows.
    Q_INVOKABLE bool trigger(const QString &text, int caret, int role = 0);
    Q_INVOKABLE bool hovers(int role) const { return m_hovers && m_hovers(role); }
    Q_INVOKABLE void hoverAt(int role, const QString &text, int position);
    Q_INVOKABLE void clearHover();
    QVariantMap hover() const { return m_hover; }
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
    void hoverChanged();

private:
    QString rowText(std::size_t entry) const;
    void ask(int role, const QString &text, int caret, const QString &trigger);
    bool tagsShown() const;
    core::taglist::Completion m_completion;
    Triggers m_triggers;
    AskProviders m_ask;
    std::vector<ProviderRow> m_provided;
    int m_providedSelection = -1;
    std::uint64_t m_asked = 0;
    Hovers m_hovers;
    AskHover m_askHover;
    QVariantMap m_hover;
    std::uint64_t m_hoverAsked = 0;
    ReadOptions m_read;
    WriteOptions m_write;
    int m_options = 0;
};

}
