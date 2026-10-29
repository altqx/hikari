#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/kanji_timer.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class KanjiTimerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList styles READ styles NOTIFY changed)
    Q_PROPERTY(QString romajiStyle READ romajiStyle NOTIFY changed)
    Q_PROPERTY(QString kanjiStyle READ kanjiStyle NOTIFY changed)
    // "", "noStyles", "same" (one style chosen twice) or "noKaraoke" (no \k Line in the romaji
    // style).
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(bool suggest READ suggest WRITE setSuggest NOTIFY changed)
    Q_PROPERTY(bool multi READ multi WRITE setMulti NOTIFY changed)
    Q_PROPERTY(bool furigana READ furigana WRITE setFurigana NOTIFY changed)
    Q_PROPERTY(bool noDictionary READ noDictionary NOTIFY changed)
    // {number, kanji, romaji, status, groups}: status is "suggested", "check", "todo", "done",
    // "applied", "changed", "noKanji" or "noRomaji"; groups is the current pair's "5 of 8 groups".
    Q_PROPERTY(QVariantList pairs READ pairs NOTIFY changed)
    Q_PROPERTY(int current READ current NOTIFY changed)
    // The current pair, {} when none: {times, duration, groups [{kind (done, cur, sug),
    // syllables [{text, cs, index}], kanji, first, characters, blank,
    // furigana {reading, typed, differs} with Furigana on and kanji in it}],
    // restSyllables, restKanji [{text, index}], detail {kanji, romaji,
    // reading, cs, blank, furigana {reading, suggestion, typed, dictionary,
    // notSung}}, noKanjiLeft, canAccept, canUnlink, canPrevious, result
    // [{tag, text}], total, earlier, later}. `groups` holds only the 60 groups
    // around the current one; `earlier` and `later` count the rest.
    Q_PROPERTY(QVariantMap pair READ pair NOTIFY changed)
    Q_PROPERTY(int pending READ pending NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
public:
    struct Hooks {
        std::function<application::EditSession *()> target;
        std::function<std::uint64_t()> targetId;             // its DocumentId (never reused)
        std::function<void()> refresh;                       // after the Document changed
        std::function<application::ReadingAnalyzer()> dictionary; // called once; none on failure
    };
    KanjiTimerController(ui::SettingsStore &settings, Hooks hooks, QObject *parent = nullptr);
    ~KanjiTimerController() override;

    QStringList styles() const { return m_styles; }
    QString romajiStyle() const { return m_romajiStyle; }
    QString kanjiStyle() const { return m_kanjiStyle; }
    QString problem() const { return m_problem; }
    bool suggest() const;
    void setSuggest(bool on);
    bool multi() const;
    void setMulti(bool on);
    bool furigana() const;
    void setFurigana(bool on);
    bool noDictionary() const;
    QVariantList pairs() const;
    int current() const;
    QVariantMap pair() const;
    int pending() const;
    bool canUndo() const;
    bool canRedo() const;

    Q_INVOKABLE void open();
    Q_INVOKABLE void close();
    Q_INVOKABLE void setStyles(const QString &romaji, const QString &kanji);
    void documentEdited();

    Q_INVOKABLE bool growSyllables();
    Q_INVOKABLE bool shrinkSyllables();
    Q_INVOKABLE bool growCharacters();
    Q_INVOKABLE bool shrinkCharacters();
    Q_INVOKABLE bool link();
    Q_INVOKABLE bool unlink();
    Q_INVOKABLE bool enter();
    Q_INVOKABLE bool acceptLine();
    Q_INVOKABLE int acceptAll();
    Q_INVOKABLE bool skipRomaji();
    Q_INVOKABLE bool skipKanji();
    Q_INVOKABLE bool previous();
    Q_INVOKABLE bool choose(int pair);
    Q_INVOKABLE bool clickSyllable(int syllable);
    Q_INVOKABLE bool clickCharacter(int character);
    Q_INVOKABLE bool setReading(const QString &reading);
    Q_INVOKABLE bool useSuggestion();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // The done pairs as one step; false for a protected or read-only Document.
    Q_INVOKABLE bool apply();
    Q_INVOKABLE void discard();

    int pendingFor(std::uint64_t document) const;
    bool applyTo(std::uint64_t document, application::EditSession &session);
    void forget(std::uint64_t document);

signals:
    void changed();

private:
    bool done(bool ok);
    void readLines();
    application::KanjiTimer *timerFor(std::uint64_t document) const;
    void configure(application::KanjiTimer &timer) const;
    void guessStyles();
    application::EditSession *target() const;

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    std::unique_ptr<application::KanjiTimer> m_timer;
    std::uint64_t m_document = 0;
    struct Kept {
        std::unique_ptr<application::KanjiTimer> timer;
        QString romajiStyle, kanjiStyle;
    };
    std::map<std::uint64_t, Kept> m_kept;
    bool m_open = false;
    QStringList m_styles;
    QString m_romajiStyle, m_kanjiStyle, m_problem;
    mutable std::optional<application::ReadingAnalyzer> m_dictionary;
};

}
