#pragma once

#include "hikari/application/font_collector.h"

#include <QSaveFile>
#include <QString>

#include <memory>
#include <vector>

namespace hikari::backends {

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
    bool unlabel() override { return true; }
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
