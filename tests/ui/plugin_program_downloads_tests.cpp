#include "hikari/app/plugin_program_downloads.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>
#include <QtTest>

#include <zlib.h>

#include <cstring>

using namespace hikari;
using app::PluginProgramDownloads;

namespace {

class FakeReply : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }
    void respond(int status, QByteArray body)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status >= 400)
            setError(ContentNotFoundError, QStringLiteral("not found"));
        m_body = std::move(body);
        emit metaDataChanged();
        emit readyRead();
        if (isFinished())
            return;
        setFinished(true);
        emit finished();
    }
    void abort() override
    {
        if (isFinished())
            return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit errorOccurred(OperationCanceledError);
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_read + QIODevice::bytesAvailable(); }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 most) override
    {
        const qint64 n = std::min(most, qint64(m_body.size()) - m_read);
        std::memcpy(data, m_body.constData() + m_read, std::size_t(n));
        m_read += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_read = 0;
};

class FakeNetwork : public QNetworkAccessManager {
public:
    QList<QPointer<FakeReply>> replies;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        auto *reply = new FakeReply(request, this);
        replies << reply;
        return reply;
    }
};

QByteArray tarGz(const QByteArray &path, const QByteArray &data)
{
    QByteArray header(512, '\0');
    header.replace(0, path.size(), path);
    header.replace(100, 7, "0000755");
    header.replace(124, 11, QByteArray::number(data.size(), 8).rightJustified(11, '0'));
    header[156] = '0';
    header.replace(257, 5, "ustar");
    header.replace(148, 8, "        ");
    unsigned sum = 0;
    for (const char c : header)
        sum += static_cast<unsigned char>(c);
    header.replace(148, 7, QByteArray::number(sum, 8).rightJustified(6, '0') + '\0');
    QByteArray tar = header + data + QByteArray((512 - data.size() % 512) % 512, '\0') + QByteArray(1024, '\0');
    z_stream s{};
    deflateInit2(&s, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(int(deflateBound(&s, uLong(tar.size()))) + 64, '\0');
    s.next_in = reinterpret_cast<Bytef *>(tar.data());
    s.avail_in = uInt(tar.size());
    s.next_out = reinterpret_cast<Bytef *>(out.data());
    s.avail_out = uInt(out.size());
    deflate(&s, Z_FINISH);
    out.resize(qsizetype(s.total_out));
    deflateEnd(&s);
    return out;
}

QByteArray zipOf(const QList<std::pair<QString, QByteArray>> &files)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QZipWriter zip(&buffer);
    for (const auto &[name, data] : files) {
        zip.setCreationPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        zip.addFile(name, data);
    }
    zip.close();
    return buffer.data();
}

}

class PluginProgramDownloadsTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_root;
    FakeNetwork m_network;
    bool m_allowed = true;
    std::unique_ptr<PluginProgramDownloads> m_downloads;

    static plugins::Manifest manifest(const QByteArray &bytes, std::string archive = {}, std::string entry = {},
                                      std::string url = "https://example.com/dl/tool")
    {
        plugins::Manifest m;
        m.id = "tester";
        plugins::Program program;
        program.id = "tool";
        program.title = "Tool";
        program.platforms[std::string(plugins::thisPlatform())] = plugins::ProgramRun{
            {},
            plugins::ProgramDownload{std::move(url),
                                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString(),
                                     bytes.size(), std::move(archive), std::move(entry)},
            {},
            {}};
        m.programs.push_back(std::move(program));
        return m;
    }
    static const plugins::ProgramRun &run(const plugins::Manifest &m)
    {
        return m.programs.front().platforms.begin()->second;
    }
    void serve(int status, const QByteArray &body)
    {
        QTRY_VERIFY(!m_network.replies.isEmpty());
        QPointer<FakeReply> reply = m_network.replies.takeLast();
        QVERIFY(reply);
        reply->respond(status, body);
    }
    QString keptFolder() const { return m_root.filePath(QStringLiteral("programs/tester")); }

private slots:
    void init()
    {
        QVERIFY(m_root.isValid());
        m_allowed = true;
        m_network.replies.clear();
        m_downloads = std::make_unique<PluginProgramDownloads>(
            PluginProgramDownloads::Wiring{m_root.filePath(QStringLiteral("programs")), &m_network, [this] { return m_allowed; }});
    }
    void cleanup()
    {
        m_downloads.reset();
        QDir(m_root.filePath(QStringLiteral("programs"))).removeRecursively();
    }

    void aProgramThatChecksOutIsKeptAndRuns()
    {
        const QByteArray bytes("#!/bin/sh\necho hi\n");
        const plugins::Manifest m = manifest(bytes);
        QCOMPARE(m_downloads->missing("tester", m).size(), std::size_t(1));
        QSignalSpy changed(m_downloads.get(), &PluginProgramDownloads::changed);
        m_downloads->fetch("tester", m);
        QVERIFY(m_downloads->fetching("tester"));
        QCOMPARE(m_network.replies.front()->url(), QUrl(QStringLiteral("https://example.com/dl/tool")));
        serve(200, bytes);
        QTRY_VERIFY(!m_downloads->fetching("tester"));
        const QString file = m_downloads->file("tester", run(m));
        QCOMPARE(QFileInfo(file).fileName(), QStringLiteral("tool"));
        QFile kept(file);
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), bytes);
#ifndef Q_OS_WIN
        QVERIFY(QFileInfo(file).isExecutable());
#endif
        QVERIFY(m_downloads->missing("tester", m).empty());
        QVERIFY(m_downloads->problem("tester").isEmpty());
        QVERIFY(changed.count() >= 1);
        QCOMPARE(QDir(keptFolder()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
                 QStringList{QString::fromStdString(run(m).download->sha256)});
    }

    void aTarGzOrZipIsUnpackedAndItsEntryKept()
    {
        const QByteArray tgz = tarGz("bin/tool", "tar tool");
        const plugins::Manifest fromTar = manifest(tgz, "tar.gz", "bin/tool");
        m_downloads->fetch("tester", fromTar);
        serve(200, tgz);
        QTRY_VERIFY(!m_downloads->file("tester", run(fromTar)).isEmpty());
#ifndef Q_OS_WIN
        QVERIFY(QFileInfo(m_downloads->file("tester", run(fromTar))).isExecutable());
#endif

        const QByteArray zip = zipOf({{QStringLiteral("app/bin/tool"), "zip tool"}, {QStringLiteral("app/lib/a.txt"), "a"}});
        const plugins::Manifest fromZip = manifest(zip, "zip", "app/bin/tool");
        m_downloads->fetch("tester", fromZip);
        serve(200, zip);
        QTRY_VERIFY(!m_downloads->file("tester", run(fromZip)).isEmpty());
        QFile tool(m_downloads->file("tester", run(fromZip)));
        QVERIFY(tool.open(QIODevice::ReadOnly));
        QCOMPARE(tool.readAll(), QByteArray("zip tool"));
    }

    void aDownloadThatIsntTheNamedFileIsDropped()
    {
        const plugins::Manifest m = manifest("the real one");
        m_downloads->fetch("tester", m);
        serve(200, "the fake one");
        QTRY_VERIFY(!m_downloads->problem("tester").isEmpty());
        QVERIFY(m_downloads->file("tester", run(m)).isEmpty());
        QVERIFY(QDir(keptFolder()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void aDownloadLargerThanNamedIsAbortedAndAnHttpErrorFails()
    {
        const plugins::Manifest m = manifest("small");
        m_downloads->fetch("tester", m);
        serve(200, QByteArray(1000, 'x'));
        QTRY_VERIFY(!m_downloads->problem("tester").isEmpty());
        QVERIFY(m_downloads->file("tester", run(m)).isEmpty());

        m_downloads->fetch("tester", m);
        QVERIFY(m_downloads->problem("tester").isEmpty());
        serve(404, {});
        QTRY_VERIFY(!m_downloads->problem("tester").isEmpty());
    }

    void anArchiveThatBreaksThePathRulesKeepsNothing()
    {
        const QByteArray tgz = tarGz("../evil", "x");
        const plugins::Manifest m = manifest(tgz, "tar.gz", "evil");
        m_downloads->fetch("tester", m);
        serve(200, tgz);
        QTRY_VERIFY(!m_downloads->problem("tester").isEmpty());
        QVERIFY(!QFile::exists(m_root.filePath(QStringLiteral("programs/evil"))));
        QVERIFY(QDir(keptFolder()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());

        const QByteArray noEntry = tarGz("bin/other", "x");
        const plugins::Manifest missingEntry = manifest(noEntry, "tar.gz", "bin/tool");
        m_downloads->fetch("tester", missingEntry);
        serve(200, noEntry);
        QTRY_VERIFY(!m_downloads->problem("tester").isEmpty());
        QVERIFY(m_downloads->file("tester", run(missingEntry)).isEmpty());
    }

    void withTheNetworkSwitchOffNothingIsFetched()
    {
        m_allowed = false;
        m_downloads->fetch("tester", manifest("x"));
        QVERIFY(m_network.replies.isEmpty());
        QVERIFY(!m_downloads->fetching("tester"));
        QVERIFY(!m_downloads->problem("tester").isEmpty());
    }

    void onlyNamedHashesAndInstalledPluginsAreKept()
    {
        const QByteArray bytes("tool");
        const plugins::Manifest m = manifest(bytes);
        m_downloads->fetch("tester", m);
        serve(200, bytes);
        QTRY_VERIFY(!m_downloads->file("tester", run(m)).isEmpty());
        QVERIFY(QDir().mkpath(keptFolder() + QStringLiteral("/0123")));
        m_downloads->keepOnly("tester", PluginProgramDownloads::hashes(m));
        QVERIFY(!QDir(keptFolder() + QStringLiteral("/0123")).exists());
        QVERIFY(!m_downloads->file("tester", run(m)).isEmpty());
        m_downloads->keepPlugins({"someone-else"});
        QVERIFY(!QDir(keptFolder()).exists());
    }
};

QTEST_GUILESS_MAIN(PluginProgramDownloadsTests)
#include "plugin_program_downloads_tests.moc"
