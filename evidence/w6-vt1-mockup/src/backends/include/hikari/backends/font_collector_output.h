#pragma once

// Y8: the font collector's output on the file system (legacy FontCollector
// MakeDirectory, CreateZip, SaveFont and CloseZip, FontCollector.cpp:877-1038).
// "Copy to selected folder" writes each font into the folder, overwriting a
// file of the same name as wxCopyFile does; "Zip" writes a deflated archive
// with UTF-8 names (wxZipOutputStream level 9, Utf8ZipEntry). Output that is
// not complete carries a label (the approved partial-output rule): a text
// file in the folder, an entry in the archive. The archive is published only
// when it is closed, so a cancelled run leaves no archive at all, and the
// previous file of that name stays until then.

#include "hikari/application/font_collector.h"

#include <QSaveFile>
#include <QString>

#include <memory>
#include <vector>

namespace hikari::backends {

// The label's file name, in the folder or the archive.
inline constexpr const char *kIncompleteLabel = "INCOMPLETE - font collection.txt";

class FolderCollectorOutput final : public application::CollectorOutput {
public:
    explicit FolderCollectorOutput(QString folder) : m_folder(std::move(folder)) {}
    bool open() override;
    bool folderFailed() const override { return m_folderFailed; }
    bool put(const std::u16string &name, const std::vector<std::byte> &bytes) override;
    bool label(const std::string &utf8Text) override;
    bool unlabel() override;
    bool commit() override { return true; }
    void discard() override {}

private:
    QString m_folder;
    bool m_folderFailed = false;
};

class ZipCollectorOutput final : public application::CollectorOutput {
public:
    explicit ZipCollectorOutput(QString archive) : m_archive(std::move(archive)) {}
    ~ZipCollectorOutput() override;
    bool open() override;
    bool folderFailed() const override { return m_folderFailed; }
    bool put(const std::u16string &name, const std::vector<std::byte> &bytes) override;
    bool label(const std::string &utf8Text) override;
    bool unlabel() override { return true; } // an archive is labelled only when it is closed incomplete
    bool commit() override;
    void discard() override;

private:
    bool add(const QByteArray &utf8Name, const QByteArray &data);
    struct Entry {
        QByteArray name;
        quint32 crc = 0, compressed = 0, size = 0, offset = 0;
    };
    QString m_archive;
    std::unique_ptr<QSaveFile> m_file;
    std::vector<Entry> m_entries;
    quint32 m_offset = 0;
    quint16 m_time = 0, m_date = 0;
    bool m_folderFailed = false;
};

} // namespace hikari::backends
