#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/qc_notes.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

// The QC notes of the editing target, kept as "Hikari QC" Script Info keys.
class QcNotesController : public QObject {
    Q_OBJECT
    // The notes the section and Hide done show, in time order: {index, time (ms), timeText, section, done,
    // text (lines joined by "\n"), row (1-based Grid row; 0 without a Line)}. `index` is what the
    // invokables take.
    Q_PROPERTY(QVariantList notes READ notes NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    // Every section in order of its first note: {name, count, open}.
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    // "" for every section.
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY changed)
    Q_PROPERTY(bool hideDone READ hideDone WRITE setHideDone NOTIFY changed)
    // The current note's index; -1 for none.
    Q_PROPERTY(int current READ current NOTIFY changed)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY changed)
    Q_PROPERTY(bool canKeepNotes READ canKeepNotes NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(QString lastSection READ lastSection NOTIFY changed)
    Q_PROPERTY(bool clipboardHasNotes READ clipboardHasNotes NOTIFY clipboardChanged)
public:
    using Marks = absl::flat_hash_map<std::uint64_t, QStringList>;
    struct Hooks {
        std::function<application::EditSession *()> target;
        std::function<bool()> editable; // the target takes content commands
        std::function<void()> refresh;  // after the Document changed
        std::function<void(core::LineId)> selectLine;
        std::function<bool(int ms)> seek;               // false without a video
        std::function<std::optional<int>()> videoTime; // nullopt without a video
        std::function<void(Marks, std::optional<core::LineId> current)> marks;
    };
    QcNotesController(ui::SettingsStore &settings, Hooks hooks, QObject *parent = nullptr);

    QVariantList notes() const;
    int total() const { return static_cast<int>(m_notes.size()); }
    QVariantList sections() const;
    QString section() const { return m_section; }
    void setSection(const QString &section);
    bool hideDone() const;
    void setHideDone(bool hide);
    int current() const;
    bool hasDocument() const;
    bool canKeepNotes() const;
    bool editable() const;
    QString lastSection() const;
    bool clipboardHasNotes() const;

    // The video's time, else the active Line's start, else 0.
    Q_INVOKABLE int addTime() const;
    // -1 without an active Line.
    Q_INVOKABLE int lineStart() const;
    // -1 without a video.
    Q_INVOKABLE int videoTime() const;

    Q_INVOKABLE bool add(int ms, const QString &section, const QString &text);
    Q_INVOKABLE bool edit(int index, int ms, const QString &section, const QString &text, bool done);
    Q_INVOKABLE bool setDone(int index, bool done);
    Q_INVOKABLE bool remove(int index);
    Q_INVOKABLE bool markAllNotDone();
    Q_INVOKABLE bool removeAll();

    // {text} or {error}: the file's contents as UTF-8.
    Q_INVOKABLE QVariantMap readFile(const QUrl &url) const;
    Q_INVOKABLE QString clipboardText() const;
    // {count, sections, existing, duplicates} for importing `text`.
    Q_INVOKABLE QVariantMap preview(const QString &text) const;
    // {added, skipped}, or {} when nothing could be written.
    Q_INVOKABLE QVariantMap importNotes(const QString &text, bool replace);
    Q_INVOKABLE QString exportText(bool includeDone, const QString &section) const;
    Q_INVOKABLE bool writeFile(const QUrl &url, const QString &text) const;

    // Selects the note's Line and moves the video to the note.
    Q_INVOKABLE bool goTo(int index);
    // The next or previous note the section and Hide done show; false at the end.
    Q_INVOKABLE bool next();
    Q_INVOKABLE bool previous();

    Q_INVOKABLE static QString timeText(int ms);
    // -1 when `text` isn't h:mm:ss or m:ss with an optional fraction.
    Q_INVOKABLE static int parseTime(const QString &text);

    void documentEdited();

signals:
    void changed();
    void clipboardChanged();

private:
    application::EditSession *target() const;
    bool shown(std::size_t index) const;
    bool write(std::vector<application::QcNote> notes, const char *step, std::optional<application::QcNote> current);
    void read();
    void publishMarks() const;
    std::optional<std::size_t> indexOfCurrent() const;
    bool step(bool forward);

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    QString m_section;
    std::vector<application::QcNote> m_notes;
    std::vector<std::optional<core::LineId>> m_lines;
    std::vector<std::pair<int, int>> m_lineSpansMs;
    std::vector<int> m_rows;
    std::optional<application::QcNote> m_current;
    std::uint64_t m_readSerial = 0;
    std::uint64_t m_readRevision = 0;
    bool m_readEditable = false;
    bool m_read = false;
};

}
