#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/core/document.h"
#include "hikari/core/text_import.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

// File › Import text…: reads a plain text file with Aegisub's text import options and adds its Lines
// to the open file as one step, or opens them as a new file.
class TextImportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(QString actorSeparator READ actorSeparator WRITE setActorSeparator NOTIFY changed)
    Q_PROPERTY(QString commentStarter READ commentStarter WRITE setCommentStarter NOTIFY changed)
    Q_PROPERTY(bool keepBlankLines READ keepBlankLines WRITE setKeepBlankLines NOTIFY changed)
    Q_PROPERTY(bool namesCarryOver READ namesCarryOver WRITE setNamesCarryOver NOTIFY changed)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY changed)
    // The first rows of {name, text, comment}.
    Q_PROPERTY(QVariantList preview READ preview NOTIFY changed)
    // Why the file can't be imported with these options; empty when it can.
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(bool canImport READ canImport NOTIFY changed)
    Q_PROPERTY(bool openFileAvailable READ openFileAvailable NOTIFY changed)
    Q_PROPERTY(bool intoOpenFile READ intoOpenFile WRITE setIntoOpenFile NOTIFY changed)
    Q_PROPERTY(QStringList styles READ styles NOTIFY changed)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY changed)
public:
    static constexpr int kPreviewRows = 20;
    struct Hooks {
        std::function<application::EditSession *()> target; // the open file; nullptr without one
        std::function<void()> refresh;                       // after the open file changed
        std::function<void(core::LoadResult, const QString &path)> openNew;
    };
    TextImportController(ui::SettingsStore &settings, Hooks hooks, QObject *parent = nullptr);

    QString fileName() const { return m_fileName; }
    QString actorSeparator() const;
    void setActorSeparator(const QString &separator);
    QString commentStarter() const;
    void setCommentStarter(const QString &starter);
    bool keepBlankLines() const { return m_options.keepBlankLines; }
    void setKeepBlankLines(bool keep);
    bool namesCarryOver() const { return m_options.namesCarryOver; }
    void setNamesCarryOver(bool carry);
    int lineCount() const { return static_cast<int>(m_lines.size()); }
    QVariantList preview() const;
    QString problem() const { return m_problem; }
    bool canImport() const { return !m_lines.empty(); }
    bool openFileAvailable() const { return m_openFileAvailable; }
    bool intoOpenFile() const { return m_intoOpenFile; }
    void setIntoOpenFile(bool into);
    QStringList styles() const { return m_styles; }
    QString style() const { return m_style; }
    void setStyle(const QString &style);

    // Reads the file; false when it can't be read.
    Q_INVOKABLE bool load(const QString &path);
    // Adds the Lines to the open file as one step, or opens them as a new file.
    Q_INVOKABLE bool import();

signals:
    void changed();

private:
    void reread();
    void optionsChanged();

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    QString m_path;
    QString m_fileName;
    std::optional<std::u8string> m_text;
    bool m_tooLarge = false;
    core::TextImportOptions m_options;
    std::vector<core::TextImportLine> m_lines;
    QString m_problem;
    bool m_openFileAvailable = false;
    bool m_intoOpenFile = false;
    QStringList m_styles;
    QString m_style;
};

}
