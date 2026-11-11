#include "hikari/app/lyrics_import_controller.h"

#include "hikari/application/lyrics_insert.h"
#include "hikari/backends/lyrics_text.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>
#include <cstring>

namespace hikari::app {

namespace {

using core::LyricsEncoding;

constexpr auto kOptions = "lyrics.importOptions";
constexpr int kMaxLength = 3'600'000;

QString qs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray bytes = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
}

}

LyricsImportController::LyricsImportController(ui::SettingsStore &settings, Hooks hooks, QObject *parent)
    : QObject(parent), m_settings(settings), m_hooks(std::move(hooks))
{
    const QJsonObject saved = QJsonDocument::fromJson(m_settings.text(kOptions).toUtf8()).object();
    m_gap = std::clamp(saved.value(QStringLiteral("gap")).toInt(m_gap), 0, kMaxLength);
    if (saved.value(QStringLiteral("gapOn")).toBool(false))
        m_options.gap = m_gap;
    m_options.lastLine = std::clamp(saved.value(QStringLiteral("lastLine")).toInt(5000), 0, kMaxLength);
}

QString LyricsImportController::title() const
{
    return m_lyrics ? qs(m_lyrics->facts.title) : QString();
}

QString LyricsImportController::artist() const
{
    return m_lyrics ? qs(m_lyrics->facts.artist) : QString();
}

QStringList LyricsImportController::encodings() const
{
    return {QStringLiteral("UTF-8"),   QStringLiteral("UTF-16 LE"), QStringLiteral("UTF-16 BE"),
            QStringLiteral("GB18030"), QStringLiteral("Big5"),      QStringLiteral("Shift_JIS"),
            QStringLiteral("EUC-KR")};
}

void LyricsImportController::setEncoding(int encoding)
{
    if (encoding < 0 || encoding >= encodings().size() || encoding == this->encoding())
        return;
    m_encoding = static_cast<LyricsEncoding>(encoding);
    reread();
}

QVariantList LyricsImportController::preview() const
{
    QVariantList rows;
    if (!m_lyrics)
        return rows;
    for (const core::LineRecord *line : m_lyrics->load.document.lines()) {
        if (rows.size() == 3)
            break;
        rows.append(QVariantMap{
                {QStringLiteral("start"), qs(core::legacy::assTimeText(line->start.value.microseconds() / 1000))},
                {QStringLiteral("end"), qs(core::legacy::assTimeText(line->end.value.microseconds() / 1000))},
                {QStringLiteral("text"), qs(line->text)}});
    }
    return rows;
}

void LyricsImportController::setIntoOpenFile(bool into)
{
    into = into && m_openFileAvailable;
    if (into == m_intoOpenFile)
        return;
    m_intoOpenFile = into;
    emit changed();
}

void LyricsImportController::setStyle(const QString &style)
{
    if (style == m_style || !m_styles.contains(style))
        return;
    m_style = style;
    emit changed();
}

void LyricsImportController::setGapOn(bool on)
{
    if (on == gapOn())
        return;
    m_options.gap = on ? std::optional<std::int64_t>(m_gap) : std::nullopt;
    saveOptions();
    reread();
}

void LyricsImportController::setGap(int ms)
{
    ms = std::clamp(ms, 0, kMaxLength);
    if (ms == m_gap)
        return;
    m_gap = ms;
    if (m_options.gap)
        m_options.gap = ms;
    saveOptions();
    reread();
}

void LyricsImportController::setLastLine(int ms)
{
    ms = std::clamp(ms, 0, kMaxLength);
    if (ms == m_options.lastLine)
        return;
    m_options.lastLine = ms;
    saveOptions();
    reread();
}

void LyricsImportController::saveOptions()
{
    const QJsonObject options{{QStringLiteral("gapOn"), gapOn()},
                              {QStringLiteral("gap"), m_gap},
                              {QStringLiteral("lastLine"), lastLine()}};
    m_settings.set(kOptions, QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact)));
}

bool LyricsImportController::load(const QString &path)
{
    QFile file(path);
    const auto format = core::lyricsFormatOf(u8(QFileInfo(path).suffix().toLower()));
    if (!format || !file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.read(static_cast<qint64>(core::kMaxLyricsFileSize) + 1);
    m_path = path;
    m_fileName = QFileInfo(path).fileName();
    m_format = *format;
    m_bytes.resize(static_cast<std::size_t>(bytes.size()));
    std::memcpy(m_bytes.data(), bytes.constData(), m_bytes.size());
    m_encrypted = core::lyricsEncrypted(m_bytes, m_format);
    m_detected = m_encoding = backends::detectLyricsEncoding(m_bytes);

    application::EditSession *target = m_hooks.target ? m_hooks.target() : nullptr;
    m_openFileAvailable = target && target->document().format() == core::SubtitleFormat::Ass;
    m_intoOpenFile = m_openFileAvailable;
    m_styles.clear();
    m_style.clear();
    if (m_openFileAvailable) {
        for (const core::StyleValues &style : core::decodeStyles(target->document()))
            if (!m_styles.contains(qs(style.name)))
                m_styles.append(qs(style.name));
        const auto active = target->selection().active;
        for (const core::LineRecord *line : target->document().lines())
            if (active && line->id == *active)
                m_style = qs(line->style);
        if (m_style.isEmpty() || !m_styles.contains(m_style))
            m_style = m_styles.contains(QStringLiteral("Default")) || m_styles.isEmpty() ? QStringLiteral("Default")
                                                                                           : m_styles.front();
        if (!m_styles.contains(m_style))
            m_styles.prepend(m_style);
    }
    reread();
    return true;
}

void LyricsImportController::reread()
{
    m_lyrics.reset();
    m_problem.clear();
    if (m_encrypted) {
        emit changed();
        return;
    }
    const auto text = backends::lyricsUtf8(m_bytes, m_encoding);
    if (!text) {
        m_problem = QCoreApplication::translate("HikariSub.Documents", "The file is not text in this encoding.");
    } else if (auto read = core::readLyrics(*text, m_format, m_options)) {
        m_lyrics = std::move(*read);
    } else if (m_hooks.describe) {
        m_problem = m_hooks.describe(m_path, read.error());
    }
    emit changed();
}

bool LyricsImportController::import()
{
    if (!m_lyrics)
        return false;
    if (m_intoOpenFile) {
        application::EditSession *target = m_hooks.target ? m_hooks.target() : nullptr;
        if (!target || !application::addLyricsLines(*target, m_lyrics->load.document, u8(m_style)))
            return false;
        if (m_hooks.refresh)
            m_hooks.refresh();
        return true;
    }
    if (!m_hooks.openNew)
        return false;
    m_hooks.openNew(std::move(m_lyrics->load), m_path);
    m_lyrics.reset();
    emit changed();
    return true;
}

}
