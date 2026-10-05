// E4: the Line editor's metadata fields, counters and switches in the shell's
// accessibility tree (the E2-a11y route; human NVDA/Orca observation stays a
// separate procedure). Each control carries its legacy label or tooltip as
// its name.

#include "a11y_tree.h"
#include "docking.h"
#include "theme.h"
#include "hikari/app/application.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using namespace hikari::testing;

class LineEditorA11y : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        enableAccessibility();
        // The application's controls style (composition.cpp chooses it), not
        // the platform's: Qt's native Windows style, which the application never
        // shows, divides by zero painting offscreen.
        hikari::ui::theme::chooseControlsStyle();
    }

    void metadataControlsAreNamed()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a11y.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                    "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, "
                    "Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n\n"
                    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,Ann,0,0,0,,first\n");
        }
        app::Application application;
        QVERIFY(application.openFile(path));
        QQmlApplicationEngine engine;
        ui::attachDocking(engine);
        engine.setInitialProperties(application.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(application.editor().hasLine());

        const auto nodes = accessibleTree(window);
        const QString tree = describe(nodes);
        const auto expect = [&](QAccessible::Role role, const char *name) {
            QVERIFY2(hasNode(nodes, role, QLatin1String(name)), qPrintable(QStringLiteral("%1\n%2").arg(QLatin1String(name), tree)));
        };
        expect(QAccessible::CheckBox, "Comment");
        expect(QAccessible::EditableText, "Layer");
        expect(QAccessible::EditableText, "Start");
        expect(QAccessible::EditableText, "End");
        expect(QAccessible::EditableText, "Duration");
        expect(QAccessible::ComboBox, "Style");
        expect(QAccessible::Button, "Edit style");
        expect(QAccessible::ComboBox, "Actor");
        expect(QAccessible::EditableText, "Left margin");
        expect(QAccessible::EditableText, "Right margin");
        expect(QAccessible::EditableText, "Vertical margin");
        expect(QAccessible::ComboBox, "Effect");
        expect(QAccessible::ComboBox, "Text position");
        expect(QAccessible::RadioButton, "Show times");
        expect(QAccessible::RadioButton, "Show frames");
        // The counters read as their text (EditBox::UpdateChars).
        expect(QAccessible::StaticText, "Wraps: 5/43");
        expect(QAccessible::StaticText, "Characters per second: 5<=15");
    }
};

QTEST_MAIN(LineEditorA11y)
#include "line_editor_a11y_tests.moc"
