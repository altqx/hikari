#include "hikari/app/text_import_controller.h"

#include "hikari/application/text_import.h"
#include "hikari/backends/text_encoding.h"
#include "hikari/core/style.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>

namespace hikari::app {

namespace {

constexpr auto kOptions = "textImport.options";
constexpr int kMaxOptionLength = 16;

QString qs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray bytes = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
}

std::span<const std::byte> asBytes(const QByteArray &bytes)
{
    return std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

QVariantMap encodingRow(std::string_view name)
{
    return {{QStringLiteral("name"), QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))},
            {QStringLiteral("label"), QString::fromStdString(backends::encodingLabel(name))}};
}

}

TextImportController::TextImportController(ui::SettingsStore &settings, Hooks hooks, QObject *parent)
    : QObject(parent), m_settings(settings), m_hooks(std::move(hooks))
{
    const QJsonObject saved = QJsonDocument::fromJson(m_settings.text(kOptions).toUtf8()).object();
    const auto text = [&](const char *key, std::u8string fallback) {
        const QJsonValue value = saved.value(QLatin1String(key));
        return value.isString() ? u8(value.toString().left(kMaxOptionLength)) : fallback;
    };
    m_options.actorSeparator = text("actorSeparator", m_options.actorSeparator);
    m_options.commentStarter = text("commentStarter", m_options.commentStarter);
    m_options.keepBlankLines = saved.value(QStringLiteral("keepBlankLines")).toBool(m_options.keepBlankLines);
    m_options.namesCarryOver = saved.value(QStringLiteral("namesCarryOver")).toBool(m_options.namesCarryOver);
}

QString TextImportController::actorSeparator() const
{
    return qs(m_options.actorSeparator);
}

void TextImportController::setActorSeparator(const QString &separator)
{
    const std::u8string value = u8(separator.left(kMaxOptionLength));
    if (value == m_options.actorSeparator)
        return;
    m_options.actorSeparator = value;
    optionsChanged();
}

QString TextImportController::commentStarter() const
{
    return qs(m_options.commentStarter);
}

void TextImportController::setCommentStarter(const QString &starter)
{
    const std::u8string value = u8(starter.left(kMaxOptionLength));
    if (value == m_options.commentStarter)
        return;
    m_options.commentStarter = value;
    optionsChanged();
}

void TextImportController::setKeepBlankLines(bool keep)
{
    if (keep == m_options.keepBlankLines)
        return;
    m_options.keepBlankLines = keep;
    optionsChanged();
}

void TextImportController::setNamesCarryOver(bool carry)
{
    if (carry == m_options.namesCarryOver)
        return;
    m_options.namesCarryOver = carry;
    optionsChanged();
}

QVariantList TextImportController::preview() const
{
    QVariantList rows;
    for (const core::TextImportLine &line : m_lines) {
        if (rows.size() == kPreviewRows)
            break;
        rows.append(QVariantMap{{QStringLiteral("name"), qs(line.actor)},
                                {QStringLiteral("text"), qs(line.text)},
                                {QStringLiteral("comment"), line.comment}});
    }
    return rows;
}

void TextImportController::setIntoOpenFile(bool into)
{
    into = into && m_openFileAvailable;
    if (into == m_intoOpenFile)
        return;
    m_intoOpenFile = into;
    emit changed();
}

void TextImportController::setStyle(const QString &style)
{
    if (style == m_style || !m_styles.contains(style))
        return;
    m_style = style;
    emit changed();
}

void TextImportController::optionsChanged()
{
    const QJsonObject options{{QStringLiteral("actorSeparator"), actorSeparator()},
                              {QStringLiteral("commentStarter"), commentStarter()},
                              {QStringLiteral("keepBlankLines"), m_options.keepBlankLines},
                              {QStringLiteral("namesCarryOver"), m_options.namesCarryOver}};
    m_settings.set(kOptions, QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact)));
    reread();
}

bool TextImportController::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.read(static_cast<qint64>(core::kMaxTextImportSize) + 1);
    m_path = path;
    m_fileName = QFileInfo(path).fileName();
    m_tooLarge = static_cast<std::size_t>(bytes.size()) > core::kMaxTextImportSize;
    m_bytes = m_tooLarge ? QByteArray() : bytes;
    m_encoding = backends::suggestedEncoding(asBytes(m_bytes)).value_or("UTF-8");
    decode();

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

void TextImportController::setEncoding(const QString &encoding)
{
    const std::string name = encoding.toStdString();
    if (name.empty() || name == m_encoding)
        return;
    m_encoding = name;
    decode();
    reread();
}

QVariantList TextImportController::encodings() const
{
    QVariantList rows;
    const auto offered = backends::reopenEncodings();
    if (std::ranges::none_of(offered, [&](const backends::TextEncoding &e) { return e.name == m_encoding; }))
        rows << encodingRow(m_encoding);
    for (const backends::TextEncoding &encoding : offered)
        rows << encodingRow(encoding.name);
    return rows;
}

void TextImportController::decode()
{
    m_text.reset();
    if (m_tooLarge)
        return;
    if (const auto utf8 = backends::utf8From(asBytes(m_bytes), m_encoding))
        m_text = std::u8string(reinterpret_cast<const char8_t *>(utf8->data()), utf8->size());
}

void TextImportController::reread()
{
    m_lines.clear();
    m_problem.clear();
    if (m_tooLarge) {
        m_problem = QCoreApplication::translate("HikariSub.Documents", "The file is larger than %n MB.", "",
                                                static_cast<int>(core::kMaxTextImportSize >> 20));
    } else if (!m_text) {
        //: %1 is an encoding's name, as in the Encoding list.
        m_problem = QCoreApplication::translate("HikariSub.Documents", "The file isn’t %1 text. Choose another encoding.")
                        .arg(QString::fromStdString(backends::encodingLabel(m_encoding)));
    } else if (auto lines = core::importText(*m_text, m_options)) {
        m_lines = std::move(*lines);
    } else if (lines.error() == core::TextImportProblem::TimecodesFile) {
        m_problem = QCoreApplication::translate("HikariSub.Documents",
                                                "This is a timecodes file. Open it with Video › Open timecodes… instead.");
    } else {
        m_problem = QCoreApplication::translate("HikariSub.Documents", "The file has no lines to import.");
    }
    emit changed();
}

bool TextImportController::import()
{
    if (m_lines.empty())
        return false;
    if (m_intoOpenFile) {
        application::EditSession *target = m_hooks.target ? m_hooks.target() : nullptr;
        if (!target || !application::addTextLines(*target, m_lines, u8(m_style)))
            return false;
        if (m_hooks.refresh)
            m_hooks.refresh();
        return true;
    }
    if (!m_hooks.openNew)
        return false;
    m_hooks.openNew(application::textImportFile(m_lines), m_path);
    return true;
}

}
