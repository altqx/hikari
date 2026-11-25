#include "hikari/app/application.h"

#include "hikari/core/ass_attachments.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVariantMap>

#include <algorithm>
#include <functional>

namespace hikari::app {

namespace {

constexpr qint64 kLargeAttachment = 10ll * 1024 * 1024;
constexpr qint64 kLargeTotal = 50ll * 1024 * 1024;

struct ReadFile {
    QString fileName;
    std::vector<std::byte> bytes;
};

std::optional<ReadFile> readFile(const QUrl &url)
{
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return std::nullopt;
    const auto *begin = reinterpret_cast<const std::byte *>(data.constData());
    return ReadFile{QFileInfo(file.fileName()).fileName(), std::vector<std::byte>(begin, begin + data.size())};
}

core::AttachmentKind kindOf(bool fonts)
{
    return fonts ? core::AttachmentKind::Font : core::AttachmentKind::Graphic;
}

QString megabytes(qint64 bytes)
{
    return QLocale().toString(double(bytes) / (1024 * 1024), 'f', 1);
}

}

bool Application::canAttach() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    return session && session->document().format() == core::SubtitleFormat::Ass &&
           m_workspace.checkContentCommand(*target).has_value();
}

QVariantList Application::attachmentRows() const
{
    QVariantList rows;
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return rows;
    for (const auto &attachment : core::attachments(session->document()))
        rows << QVariantMap{{QStringLiteral("name"), QString::fromStdString(attachment.name)},
                            {QStringLiteral("font"), attachment.kind == core::AttachmentKind::Font},
                            {QStringLiteral("size"), qint64(attachment.size)}};
    return rows;
}

QStringList Application::attachmentWarnings(bool fonts, const QList<QUrl> &files) const
{
    QStringList warnings;
    qint64 total = 0;
    for (const auto &row : attachmentRows())
        total += row.toMap().value(QStringLiteral("size")).toLongLong();
    for (const QUrl &url : files) {
        const auto file = readFile(url);
        if (!file)
            continue;
        const auto size = qint64(file->bytes.size());
        total += size;
        if (size > kLargeAttachment)
            warnings << QCoreApplication::translate("HikariSub.Attachments", "“%1” is %2 MB, over 10 MB.")
                            .arg(file->fileName, megabytes(size));
        if (fonts && core::embeddingRestricted(file->bytes))
            warnings << QCoreApplication::translate("HikariSub.Attachments",
                                                    "The licence of “%1” restricts embedding it in other files.")
                            .arg(file->fileName);
    }
    if (total > kLargeTotal)
        warnings << QCoreApplication::translate("HikariSub.Attachments", "The attachments would total %1 MB, over 50 MB.")
                        .arg(megabytes(total));
    return warnings;
}

bool Application::attachFiles(bool fonts, const QList<QUrl> &files)
{
    const QString title = QCoreApplication::translate("HikariSub.Attachments", "Attachments");
    std::vector<ReadFile> read;
    for (const QUrl &url : files) {
        auto file = readFile(url);
        if (!file) {
            m_log->notice(ui::LogController::Error, title,
                          QCoreApplication::translate("HikariSub.Attachments", "Could not read “%1”; nothing was attached.")
                              .arg(QFileInfo(url.toLocalFile()).fileName()));
            return false;
        }
        read.push_back(std::move(*file));
    }
    if (read.empty() || !canAttach())
        return false;
    const auto kind = kindOf(fonts);
    return runFilter([&](application::EditSession &s) -> std::expected<void, application::CommandRefusal> {
        return s.run(application::Command{fonts ? "Attach fonts" : "Attach graphics", s.revision(), {},
                                          [&](core::Document &d) {
                                              for (const auto &file : read) {
                                                  const QByteArray name = file.fileName.toUtf8();
                                                  const std::u8string_view utf8(
                                                      reinterpret_cast<const char8_t *>(name.constData()),
                                                      std::size_t(name.size()));
                                                  if (!d.addAttachment(kind, core::attachmentName(kind, utf8), file.bytes))
                                                      return false;
                                              }
                                              return true;
                                          }});
    });
}

bool Application::removeAttachments(QList<int> rows)
{
    std::ranges::sort(rows, std::greater{});
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.isEmpty() || rows.back() < 0 || !canAttach())
        return false;
    return runFilter([&](application::EditSession &s) -> std::expected<void, application::CommandRefusal> {
        return s.run(application::Command{"Remove attachment", s.revision(), {}, [&](core::Document &d) {
                                              return std::ranges::all_of(rows, [&](int row) {
                                                  return d.removeAttachment(std::size_t(row));
                                              });
                                          }});
    });
}

QUrl Application::attachmentFile(const QUrl &folder, int row) const
{
    const auto rows = attachmentRows();
    if (row < 0 || row >= rows.size())
        return {};
    QString name = rows.at(row).toMap().value(QStringLiteral("name")).toString();
    name.replace(u'\\', u'/');
    name = QFileInfo(name).fileName();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return {};
    return QUrl::fromLocalFile(QDir(folder.toLocalFile()).filePath(name));
}

QString Application::extractAttachment(int row, const QUrl &file, bool replace)
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session || row < 0 || file.isEmpty())
        return QStringLiteral("failed");
    const QString path = file.toLocalFile();
    if (!replace && QFileInfo::exists(path))
        return QStringLiteral("exists");
    const auto bytes = core::attachmentBytes(session->document(), std::size_t(row));
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size())) != qint64(bytes.size()) ||
        !out.commit())
        return QStringLiteral("failed");
    return QStringLiteral("written");
}

}
