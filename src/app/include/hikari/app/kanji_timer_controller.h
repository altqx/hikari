#pragma once

// KA3 (#278): the Kanji timer window (Subtitles > Kanji timer…,
// GLOBAL_KANJI_TIMER) over the editing target: application::KanjiTimer's
// pairs and the current pair's grouping as the window draws them, its
// commands, and Apply, the done pairs' kanji Lines as one "Kanji timing" step.

#include "hikari/application/edit_session.h"
#include "hikari/application/kanji_timer.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class KanjiTimerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList styles READ styles NOTIFY changed)
    Q_PROPERTY(QString romajiStyle READ romajiStyle NOTIFY changed)
    Q_PROPERTY(QString kanjiStyle READ kanjiStyle NOTIFY changed)
    // "" or why there is nothing to pair: "noStyles" (the target has no ASS
    // Styles), "same" (one style chosen twice), "noKaraoke" (no \k Line in
    // the romaji style).
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(bool suggest READ suggest WRITE setSuggest NOTIFY changed)
    // {number, kanji, romaji, status, groups}: status is "suggested",
    // "check", "todo", "done", "applied", "changed", "noKanji", "noRomaji";
    // groups the current pair's "5 of 8 groups".
    Q_PROPERTY(QVariantList pairs READ pairs NOTIFY changed)
    Q_PROPERTY(int current READ current NOTIFY changed)
    // The current pair: {times, duration, groups [{kind (done, cur, sug),
    // syllables [{text, cs, index}], kanji, first, characters, blank}],
    // restSyllables, restKanji [{text, index}], detail {kanji, romaji,
    // reading, cs, blank}, noKanjiLeft, canAccept, canUnlink, canPrevious,
    // result [{tag, text}], total}; {} when there is none.
    Q_PROPERTY(QVariantMap pair READ pair NOTIFY changed)
    Q_PROPERTY(int pending READ pending NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
public:
    struct Hooks {
        std::function<application::EditSession *()> target; // the editing target
        std::function<void()> refresh;                       // after the Document changed
    };
    KanjiTimerController(ui::SettingsStore &settings, Hooks hooks, QObject *parent = nullptr);
    ~KanjiTimerController() override;

    QStringList styles() const { return m_styles; }
    QString romajiStyle() const { return m_romajiStyle; }
    QString kanjiStyle() const { return m_kanjiStyle; }
    QString problem() const { return m_problem; }
    bool suggest() const;
    void setSuggest(bool on);
    QVariantList pairs() const;
    int current() const;
    QVariantMap pair() const;
    int pending() const;
    bool canUndo() const;
    bool canRedo() const;

    // The window opened: the target's Lines read and, the first time for
    // this Document, the styles guessed (the style with the most \k Lines,
    // then one named kanji or jp, else the next with Lines).
    Q_INVOKABLE void open();
    Q_INVOKABLE void close();
    Q_INVOKABLE void setStyles(const QString &romaji, const QString &kanji);
    // The editing target or one of its Lines changed (Application::refreshViews).
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
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // KA3-apply: the done pairs into the Document as one step; false when it
    // refused (a protected or read-only Document).
    Q_INVOKABLE bool apply();
    // Close without applying: the done pairs are dropped.
    Q_INVOKABLE void discard();

signals:
    void changed();

private:
    bool done(bool ok);
    void readLines();
    void guessStyles();
    application::EditSession *target() const;

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    std::unique_ptr<application::KanjiTimer> m_timer;
    const application::EditSession *m_session = nullptr; // the Document the timer is for
    bool m_open = false;
    QStringList m_styles;
    QString m_romajiStyle, m_kanjiStyle, m_problem;
};

} // namespace hikari::app
