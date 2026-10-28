#include <QGuiApplication>
#include <QTest>

class QtTestControls : public QObject
{
    Q_OBJECT
private slots:
    void passes() { QCOMPARE(1 + 1, 2); }
    void guiPlatformLoads() { QVERIFY(!QGuiApplication::platformName().isEmpty()); }
};

QTEST_MAIN(QtTestControls)
#include "qttest_controls.moc"
