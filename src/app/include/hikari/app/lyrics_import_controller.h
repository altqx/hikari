#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/core/lyrics_import.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

// File › Import lyrics…: reads a lyrics file in a chosen encoding and adds its Lines
// to the open file as one step, or opens them as a new file.
class LyricsImportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    // 0 LRC, 1 TTML, 2 KRC, 3 QRC.
    Q_PROPERTY(int format READ format NOTIFY changed)
    Q_PROPERTY(bool wordTimed READ wordTimed NOTIFY changed)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(int offset READ offset NOTIFY changed)
    Q_PROPERTY(QStringList encodings READ encodings CONSTANT)
    Q_PROPERTY(int encoding READ encoding WRITE setEncoding NOTIFY changed)
    Q_PROPERTY(int detectedEncoding READ detectedEncoding NOTIFY changed)
    // Up to three rows of {start, end, text}.
    Q_PROPERTY(QVariantList preview READ preview NOTIFY changed)
    Q_PROPERTY(bool encrypted READ encrypted NOTIFY changed)
    // Why the file can't be imported in this encoding; empty when it can.
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(bool canImport READ canImport NOTIFY changed)
    Q_PROPERTY(bool openFileAvailable READ openFileAvailable NOTIFY changed)
    Q_PROPERTY(bool intoOpenFile READ intoOpenFile WRITE setIntoOpenFile NOTIFY changed)
    Q_PROPERTY(QStringList styles READ styles NOTIFY changed)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY changed)
    Q_PROPERTY(bool gapOn READ gapOn WRITE setGapOn NOTIFY changed)
    Q_PROPERTY(int gap READ gap WRITE setGap NOTIFY changed)           // ms
    Q_PROPERTY(int lastLine READ lastLine WRITE setLastLine NOTIFY changed) // ms
public:
    struct Hooks {
        std::function<application::EditSession *()> target; // the open file; nullptr without one
        std::function<void()> refresh;                       // after the open file changed
        std::function<void(core::LoadResult, const QString &path)> openNew;
        std::function<QString(const QString &path, const core::LyricsProblem &)> describe;
    };
    LyricsImportController(ui::SettingsStore &settings, Hooks hooks, QObject *parent = nullptr);

    QString fileName() const { return m_fileName; }
    int format() const { return static_cast<int>(m_format); }
    bool wordTimed() const { return m_lyrics && m_lyrics->facts.wordTimed; }
    int lineCount() const { return m_lyrics ? static_cast<int>(m_lyrics->facts.lines) : 0; }
    QString title() const;
    QString artist() const;
    int offset() const { return m_lyrics ? static_cast<int>(m_lyrics->facts.offset) : 0; }
    QStringList encodings() const;
    int encoding() const { return static_cast<int>(m_encoding); }
    void setEncoding(int encoding);
    int detectedEncoding() const { return static_cast<int>(m_detected); }
    QVariantList preview() const;
    bool encrypted() const { return m_encrypted; }
    QString problem() const { return m_problem; }
    bool canImport() const { return m_lyrics.has_value(); }
    bool openFileAvailable() const { return m_openFileAvailable; }
    bool intoOpenFile() const { return m_intoOpenFile; }
    void setIntoOpenFile(bool into);
    QStringList styles() const { return m_styles; }
    QString style() const { return m_style; }
    void setStyle(const QString &style);
    bool gapOn() const { return m_options.gap.has_value(); }
    void setGapOn(bool on);
    int gap() const { return m_gap; }
    void setGap(int ms);
    int lastLine() const { return static_cast<int>(m_options.lastLine); }
    void setLastLine(int ms);

    // Reads the file and detects its encoding; false when it can't be read.
    Q_INVOKABLE bool load(const QString &path);
    // Adds the Lines to the open file as one step, or opens them as a new file.
    Q_INVOKABLE bool import();

signals:
    void changed();

private:
    void reread();
    void saveOptions();

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    QString m_path;
    QString m_fileName;
    std::vector<std::byte> m_bytes;
    core::LyricsFormat m_format = core::LyricsFormat::Lrc;
    core::LyricsEncoding m_encoding = core::LyricsEncoding::Utf8;
    core::LyricsEncoding m_detected = core::LyricsEncoding::Utf8;
    core::LyricsOptions m_options;
    int m_gap = 200;
    std::optional<core::ImportedLyrics> m_lyrics;
    bool m_encrypted = false;
    QString m_problem;
    bool m_openFileAvailable = false;
    bool m_intoOpenFile = false;
    QStringList m_styles;
    QString m_style;
};

}
