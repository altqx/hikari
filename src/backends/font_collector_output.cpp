#include "hikari/backends/font_collector_output.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <array>
#include <cstring>

namespace hikari::backends {

namespace {

QString qs(const std::u16string &s)
{
    return QString::fromUtf16(s.data(), qsizetype(s.size()));
}

quint32 crc32(const QByteArray &data)
{
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (const char b : data)
        c = table[(c ^ quint8(b)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void le16(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF));
}

void le32(QByteArray &out, quint32 v)
{
    le16(out, v & 0xFFFF);
    le16(out, v >> 16);
}

} // namespace

bool FolderCollectorOutput::open()
{
    // MakeDirectory (FontCollector.cpp:1000-1015).
    if (!QDir(m_folder).exists() && !QDir().mkpath(m_folder)) {
        m_folderFailed = true;
        return false;
    }
    return true;
}

bool FolderCollectorOutput::put(const std::u16string &name, const std::vector<std::byte> &bytes)
{
    // wxCopyFile overwrites a file of the same name.
    QFile file(QDir(m_folder).filePath(qs(name)));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 n = file.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size()));
    return n == qint64(bytes.size()) && file.flush();
}

bool FolderCollectorOutput::label(const std::string &utf8Text)
{
    QFile file(QDir(m_folder).filePath(QString::fromUtf8(kIncompleteLabel)));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(utf8Text.data(), qint64(utf8Text.size())) == qint64(utf8Text.size()) && file.flush();
}

bool FolderCollectorOutput::unlabel()
{
    const QString path = QDir(m_folder).filePath(QString::fromUtf8(kIncompleteLabel));
    return !QFile::exists(path) || QFile::remove(path);
}

ZipCollectorOutput::~ZipCollectorOutput()
{
    if (m_file)
        m_file->cancelWriting();
}

bool ZipCollectorOutput::open()
{
    // MakeDirectory(isZip) and CreateZip (FontCollector.cpp:1000-1029).
    const QString folder = QFileInfo(m_archive).absolutePath();
    if (!QDir(folder).exists() && !QDir().mkpath(folder)) {
        m_folderFailed = true;
        return false;
    }
    m_file = std::make_unique<QSaveFile>(m_archive);
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_file.reset();
        return false;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    m_date = quint16(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
    m_time = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    return true;
}

bool ZipCollectorOutput::add(const QByteArray &utf8Name, const QByteArray &data)
{
    if (!m_file)
        return false;
    // Deflate at level 9: qCompress's zlib stream without its 4-byte size
    // prefix, 2-byte header and 4-byte Adler-32 trailer.
    const QByteArray z = qCompress(data, 9);
    if (z.size() < 10)
        return false;
    const QByteArray deflated = z.mid(6, z.size() - 10);
    Entry e;
    e.name = utf8Name;
    e.crc = crc32(data);
    e.compressed = quint32(deflated.size());
    e.size = quint32(data.size());
    e.offset = m_offset;
    QByteArray header;
    le32(header, 0x04034b50);
    le16(header, 20);     // version needed
    le16(header, 0x0800); // UTF-8 names (Utf8ZipEntry)
    le16(header, 8);      // deflate
    le16(header, m_time);
    le16(header, m_date);
    le32(header, e.crc);
    le32(header, e.compressed);
    le32(header, e.size);
    le16(header, quint32(utf8Name.size()));
    le16(header, 0);
    header += utf8Name;
    if (m_file->write(header) != header.size() || m_file->write(deflated) != deflated.size())
        return false;
    m_offset += quint32(header.size() + deflated.size());
    m_entries.push_back(e);
    return true;
}

bool ZipCollectorOutput::put(const std::u16string &name, const std::vector<std::byte> &bytes)
{
    return add(qs(name).toUtf8(), QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())));
}

bool ZipCollectorOutput::label(const std::string &utf8Text)
{
    return add(QByteArray(kIncompleteLabel), QByteArray(utf8Text.data(), qsizetype(utf8Text.size())));
}

bool ZipCollectorOutput::commit()
{
    if (!m_file)
        return false;
    QByteArray directory;
    for (const auto &e : m_entries) {
        le32(directory, 0x02014b50);
        le16(directory, 20); // version made by
        le16(directory, 20);
        le16(directory, 0x0800);
        le16(directory, 8);
        le16(directory, m_time);
        le16(directory, m_date);
        le32(directory, e.crc);
        le32(directory, e.compressed);
        le32(directory, e.size);
        le16(directory, quint32(e.name.size()));
        le16(directory, 0); // extra
        le16(directory, 0); // comment
        le16(directory, 0); // disk
        le16(directory, 0); // internal attributes
        le32(directory, 0); // external attributes
        le32(directory, e.offset);
        directory += e.name;
    }
    QByteArray end;
    le32(end, 0x06054b50);
    le16(end, 0);
    le16(end, 0);
    le16(end, quint32(m_entries.size()));
    le16(end, quint32(m_entries.size()));
    le32(end, quint32(directory.size()));
    le32(end, m_offset);
    le16(end, 0);
    const bool ok = m_file->write(directory) == directory.size() && m_file->write(end) == end.size() && m_file->commit();
    m_file.reset();
    return ok;
}

void ZipCollectorOutput::discard()
{
    if (m_file)
        m_file->cancelWriting();
    m_file.reset();
}

} // namespace hikari::backends
