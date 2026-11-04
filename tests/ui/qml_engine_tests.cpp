#include "hikari/app/local_only_network.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

namespace {

constexpr int kImageReady = 1;

}

class QmlEngineTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QObject> create(QQmlEngine &engine, const QByteArray &qml)
    {
        QQmlComponent component(&engine);
        component.setData(qml, QUrl(QStringLiteral("qrc:/qml-engine-test.qml")));
        if (!component.isReady())
            qWarning() << component.errors();
        return std::unique_ptr<QObject>(component.create());
    }

    static QNetworkReply *getFromLoopback(QQmlEngine &engine, const QTcpServer &server)
    {
        const QUrl url(QStringLiteral("http://127.0.0.1:%1/image.png").arg(server.serverPort()));
        return engine.networkAccessManager()->get(QNetworkRequest(url));
    }

private slots:
    void remoteRequestsNeverLeaveTheMachine()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QQmlEngine engine;
        hikari::app::refuseRemoteUrls(engine);
        QNetworkReply *reply = getFromLoopback(engine, server);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait());
        QVERIFY(reply->error() != QNetworkReply::NoError);
        QVERIFY(!server.waitForNewConnection(0));
    }

    void remoteRequestsReachTheServerWithoutTheBlock()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QQmlEngine engine;
        getFromLoopback(engine, server);
        QTRY_VERIFY(server.hasPendingConnections());
    }

    void localImagesStillLoad()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("local.png"));
        QImage pixel(4, 4, QImage::Format_ARGB32);
        pixel.fill(Qt::red);
        QVERIFY(pixel.save(path));
        QQmlEngine engine;
        hikari::app::refuseRemoteUrls(engine);
        const auto root = create(engine, "import QtQuick\nImage { source: '" + QUrl::fromLocalFile(path).toEncoded() + "' }\n");
        QVERIFY(root);
        QTRY_COMPARE(root->property("status").toInt(), kImageReady);
    }

    void placeholdersInSubstitutedTextStayAsWritten()
    {
        QQmlEngine engine;
        const auto note = create(engine, "import Hikari.Ui\nFontResolutionNote {\n"
                                         "    resolution: ({ kind: 'fallback', family: 'Discount %2 Sans', file: 'discount.ttf' })\n"
                                         "}\n");
        QVERIFY(note);
        QCOMPARE(note->property("text").toString(),
                 QStringLiteral("Not installed: the subtitle renderer falls back to Discount %2 Sans (discount.ttf)."));
    }

    void spellingMarksAreAWavyUnderline()
    {
        QQmlEngine engine;
        const auto edit = create(engine, "import QtQuick\nimport Hikari.Ui\nTextEdit {\n"
                                         "    id: edit\n"
                                         "    text: 'She has seen teh last episode'\n"
                                         "    EditorHighlighter { objectName: 'marks'; document: edit.textDocument;"
                                         " colour: 'red'; ranges: [13, 16] }\n"
                                         "}\n");
        QVERIFY(edit);
        auto *marks = edit->findChild<QObject *>(QStringLiteral("marks"));
        QVariantList applied;
        QVERIFY(QMetaObject::invokeMethod(marks, "appliedRanges", Q_RETURN_ARG(QVariantList, applied)));
        QCOMPARE(applied, (QVariantList{13, 16}));
    }
};

QTEST_MAIN(QmlEngineTests)
#include "qml_engine_tests.moc"
