#include <QTest>

class QtTestFailingControl : public QObject
{
    Q_OBJECT
private slots:
    void failsOnPurpose() { QCOMPARE(1 + 1, 3); }
};

QTEST_MAIN(QtTestFailingControl)
#include "qttest_failing_control.moc"
