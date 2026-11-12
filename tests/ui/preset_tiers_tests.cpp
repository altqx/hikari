#include "presets_file.h"
#include "script_dialog_memory.h"
#include "settings_store.h"
#include "shift_times_controller.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;
using application::Tier;
using ui::PresetsFile;
using ui::ScriptDialogMemory;
using ui::SettingsStore;
using ui::ShiftTimesController;

namespace {

QString preset(const char *name, const QJsonObject &values)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("name"), QLatin1String(name)}, {QStringLiteral("values"), values}})
            .toJson(QJsonDocument::Compact));
}

QStringList rowKeys(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &row : rows) {
        const auto m = row.toMap();
        out << m.value(QStringLiteral("tier")).toString() + QLatin1Char(':') + m.value(QStringLiteral("name")).toString()
                   + (m.value(QStringLiteral("hidden")).toBool() ? QStringLiteral(" (hidden)") : QString());
    }
    return out;
}

QStringList presetNames(const QStringList &stored)
{
    QStringList out;
    for (const QString &row : stored)
        out << QJsonDocument::fromJson(row.toUtf8()).object().value(QStringLiteral("name")).toString();
    return out;
}

QVariantList choose(const char *name, const char *action, const char *newName = "")
{
    return {QVariantMap{{QStringLiteral("name"), QLatin1String(name)},
                        {QStringLiteral("action"), QLatin1String(action)},
                        {QStringLiteral("newName"), QLatin1String(newName)}}};
}

void writeAll(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}

class PresetTiersTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString userFile() const { return dir.filePath(QStringLiteral("user/settings.toml")); }
    QString project() const { return dir.filePath(QStringLiteral("show")); }
    QString presetsFile() const { return project() + QStringLiteral("/.hikari/presets.toml"); }

private slots:
    void init()
    {
        QVERIFY(dir.isValid());
        QDir(dir.path()).removeRecursively();
        QDir().mkpath(dir.filePath(QStringLiteral("user")));
        QDir().mkpath(project());
    }

    void thePresetsFileReadsTheSettingsFilesForm()
    {
        writeAll(presetsFile(), "version = 1\n[automation.dialogPresets]\nk1 = [{name = \"Soft\", values = {blur = 2.5}}]\n"
                                "[shiftTimes]\npresets = ['{\"name\":\"Late\",\"values\":{\"timeMs\":900}}']\n");
        PresetsFile file(project());
        QCOMPARE(file.list("automation.dialogPresets"),
                 QStringList{QStringLiteral("k1\t[{\"name\":\"Soft\",\"values\":{\"blur\":2.5}}]")});
        QCOMPARE(presetNames(file.list("shiftTimes.presets")), QStringList{QStringLiteral("Late")});
        QVERIFY(file.list("postprocessor.presets").isEmpty());

        QVERIFY(file.set("postprocessor.presets", {preset("Snap", {{QStringLiteral("leadIn"), 100}})}));
        const QByteArray text = readAll(presetsFile());
        QVERIFY2(text.contains("[automation.dialogPresets]"), text.constData());
        QCOMPARE(presetNames(PresetsFile(project()).list("postprocessor.presets")), QStringList{QStringLiteral("Snap")});
        QVERIFY(!PresetsFile().set("shiftTimes.presets", {preset("Late", {})}));
    }

    void shiftPresetsHideMineByNameAndResolveTheProjectFirst()
    {
        {
            SettingsStore store(userFile());
            store.set("shiftTimes.presets", QStringList{preset("Late", {{QStringLiteral("timeMs"), 100}}),
                                                        preset("Own", {{QStringLiteral("timeMs"), 200}})});
            store.set("shiftTimes.preset", QStringLiteral("Late"));
            store.sync();
        }
        writeAll(presetsFile(), "version = 1\n[shiftTimes]\npresets = ['{\"name\":\"Late\",\"values\":{\"timeMs\":900}}']\n");
        SettingsStore store(userFile());
        ShiftTimesController shift(store);
        QVERIFY(!shift.hasProject());
        QCOMPARE(rowKeys(shift.shiftPresetRows()), (QStringList{QStringLiteral("mine:Late"), QStringLiteral("mine:Own")}));

        shift.setProjectRoot(project());
        QVERIFY(shift.hasProject());
        QCOMPARE(rowKeys(shift.shiftPresetRows()), (QStringList{QStringLiteral("project:Late"),
                                                                QStringLiteral("mine:Late (hidden)"), QStringLiteral("mine:Own")}));
        QCOMPARE(shift.shiftPresetNames(), (QStringList{QStringLiteral("Late"), QStringLiteral("Own")}));
        QCOMPARE(shift.shiftPresetTier(), QStringLiteral("project"));

        QVERIFY(shift.applyPreset(QStringLiteral("shift"), QStringLiteral("Late")));
        QCOMPARE(shift.settings().timeMs, 900);
        QVERIFY(shift.applyPreset(QStringLiteral("shift"), QStringLiteral("Late"), QStringLiteral("mine")));
        QCOMPARE(shift.settings().timeMs, 100);
        QCOMPARE(shift.shiftPresetTier(), QStringLiteral("mine"));
        QVERIFY(!shift.renamePreset(QStringLiteral("shift"), QStringLiteral("Late"), QStringLiteral("Early"),
                                    QStringLiteral("mine")));
        QVERIFY(!shift.deletePreset(QStringLiteral("shift"), QStringLiteral("Late"), QStringLiteral("mine")));
        QCOMPARE(store.text("shiftTimes.preset"), QStringLiteral("Late"));

        QVERIFY(shift.renamePreset(QStringLiteral("shift"), QStringLiteral("Late"), QStringLiteral("Early"),
                                   QStringLiteral("project")));
        QCOMPARE(presetNames(PresetsFile(project()).list("shiftTimes.presets")), QStringList{QStringLiteral("Early")});
        QCOMPARE(presetNames(store.list("shiftTimes.presets")), (QStringList{QStringLiteral("Late"), QStringLiteral("Own")}));
        QCOMPARE(rowKeys(shift.shiftPresetRows()), (QStringList{QStringLiteral("project:Early"), QStringLiteral("mine:Late"),
                                                                QStringLiteral("mine:Own")}));

        shift.setProjectRoot({});
        QCOMPARE(rowKeys(shift.shiftPresetRows()), (QStringList{QStringLiteral("mine:Late"), QStringLiteral("mine:Own")}));
    }

    void postprocessorPresetsCopyAndMoveThroughTheClashReview()
    {
        SettingsStore store(userFile());
        store.set("postprocessor.presets", QStringList{preset("Snap", {{QStringLiteral("leadIn"), 50}}),
                                                       preset("Gaps", {{QStringLiteral("leadIn"), 70}})});
        writeAll(presetsFile(), "version = 1\n[postprocessor]\npresets = ['{\"name\":\"Snap\",\"values\":{\"leadIn\":300}}']\n");
        ShiftTimesController shift(store);
        shift.setProjectRoot(project());
        const QString pp = QStringLiteral("postprocessor");

        const auto clashes = shift.presetClashes(pp, QStringLiteral("Snap"), QStringLiteral("mine"));
        QCOMPARE(clashes.size(), 1);
        QCOMPARE(clashes.first().toMap().value(QStringLiteral("suggestion")).toString(), QStringLiteral("Snap (2)"));
        QVERIFY(shift.presetClashes(pp, QStringLiteral("Gaps"), QStringLiteral("mine")).isEmpty());

        QCOMPARE(shift.copyPreset(pp, QStringLiteral("Snap"), QStringLiteral("mine"), choose("Snap", "skip")), QString());
        QCOMPARE(shift.copyPreset(pp, QStringLiteral("Snap"), QStringLiteral("mine"), choose("Snap", "rename")),
                 QStringLiteral("Snap (2)"));
        QCOMPARE(presetNames(PresetsFile(project()).list("postprocessor.presets")),
                 (QStringList{QStringLiteral("Snap"), QStringLiteral("Snap (2)")}));
        QCOMPARE(presetNames(store.list("postprocessor.presets")), (QStringList{QStringLiteral("Snap"), QStringLiteral("Gaps")}));

        QCOMPARE(shift.movePreset(pp, QStringLiteral("Gaps"), QStringLiteral("mine")), QStringLiteral("Gaps"));
        QCOMPARE(presetNames(store.list("postprocessor.presets")), QStringList{QStringLiteral("Snap")});
        QCOMPARE(presetNames(PresetsFile(project()).list("postprocessor.presets")),
                 (QStringList{QStringLiteral("Snap"), QStringLiteral("Snap (2)"), QStringLiteral("Gaps")}));

        QCOMPARE(shift.copyPreset(pp, QStringLiteral("Snap"), QStringLiteral("project"), choose("Snap", "replace")),
                 QStringLiteral("Snap"));
        QVERIFY(shift.applyPreset(pp, QStringLiteral("Snap"), QStringLiteral("mine")));
        QCOMPARE(shift.settings().leadIn, 300);

        shift.setTimingPreset(QStringLiteral("Gaps"));
        QCOMPARE(store.text("postprocessor.timingPreset"), QStringLiteral("Gaps"));
        QVERIFY(shift.applyPreset(pp, shift.timingPreset()));
        QCOMPARE(shift.settings().leadIn, 70);
        QCOMPARE(shift.postprocessorPresetTier(), QStringLiteral("project"));
    }

    void savingGoesToTheChosenTierAndOpeningWritesNothing()
    {
        SettingsStore store(userFile());
        ShiftTimesController shift(store);
        shift.setProjectRoot(project());
        shift.setSettingsMap({{QStringLiteral("timeMs"), 1500}});
        QVERIFY(!QFile::exists(presetsFile()));

        QVERIFY(shift.savePreset(QStringLiteral("shift"), QStringLiteral("House"), QStringLiteral("project")));
        QCOMPARE(presetNames(PresetsFile(project()).list("shiftTimes.presets")), QStringList{QStringLiteral("House")});
        QVERIFY(store.list("shiftTimes.presets").isEmpty());
        QCOMPARE(shift.shiftPresetTier(), QStringLiteral("project"));
        QVERIFY(shift.savePreset(QStringLiteral("shift"), QStringLiteral("House"), QStringLiteral("mine")));
        QCOMPARE(presetNames(store.list("shiftTimes.presets")), QStringList{QStringLiteral("House")});
        QCOMPARE(presetNames(PresetsFile(project()).list("shiftTimes.presets")), QStringList{QStringLiteral("House")});

        shift.setProjectRoot({});
        QVERIFY(shift.savePreset(QStringLiteral("shift"), QStringLiteral("Alone"), QStringLiteral("project")));
        QCOMPARE(presetNames(store.list("shiftTimes.presets")), (QStringList{QStringLiteral("House"), QStringLiteral("Alone")}));
    }

    void dialogPresetsHideByDialogKeyAndName()
    {
        SettingsStore store(userFile());
        ScriptDialogMemory memory(&store);
        memory.savePreset(QStringLiteral("k1"), QStringLiteral("Soft"), {{"blur", 1}});
        memory.savePreset(QStringLiteral("k2"), QStringLiteral("Soft"), {{"blur", 2}});
        memory.savePreset(QStringLiteral("k1"), QStringLiteral("Lost"), {{"blur", 3}}, Tier::Project);
        QVERIFY(!QFile::exists(presetsFile()));
        QCOMPARE(memory.presets(QStringLiteral("k1")), QStringList{QStringLiteral("Soft")});
        memory.savePreset(QStringLiteral("k1"), QStringLiteral("Own"), {{"blur", 3}});

        memory.setProjectRoot(project());
        memory.savePreset(QStringLiteral("k1"), QStringLiteral("Soft"), {{"blur", 9}}, Tier::Project);
        const QByteArray text = readAll(presetsFile());
        QVERIFY2(text.contains("dialogPresets") && !text.contains("\\t["), text.constData());
        QCOMPARE(rowKeys(memory.presetRows(QStringLiteral("k1"))),
                 (QStringList{QStringLiteral("project:Soft"), QStringLiteral("mine:Soft (hidden)"), QStringLiteral("mine:Own")}));
        QCOMPARE(rowKeys(memory.presetRows(QStringLiteral("k2"))), QStringList{QStringLiteral("mine:Soft")});
        QCOMPARE(memory.presets(QStringLiteral("k1")), (QStringList{QStringLiteral("Soft"), QStringLiteral("Own")}));
        QCOMPARE(std::get<double>(memory.preset(QStringLiteral("k1"), QStringLiteral("Soft"))->at("blur")), 9.0);
        QCOMPARE(std::get<double>(memory.preset(QStringLiteral("k1"), QStringLiteral("Soft"), Tier::Mine)->at("blur")), 1.0);
        QCOMPARE(std::get<double>(memory.preset(QStringLiteral("k2"), QStringLiteral("Soft"))->at("blur")), 2.0);

        memory.deletePreset(QStringLiteral("k1"), QStringLiteral("Soft"), Tier::Mine);
        QCOMPARE(memory.presets(QStringLiteral("k1")).size(), 2);
        QCOMPARE(rowKeys(memory.presetRows(QStringLiteral("k1"))).size(), 3);

        QCOMPARE(memory.presetClash(QStringLiteral("k1"), QStringLiteral("Soft"), Tier::Mine), QStringLiteral("Soft (2)"));
        QVERIFY(!memory.presetClash(QStringLiteral("k1"), QStringLiteral("Own"), Tier::Mine));
        QCOMPARE(memory.sendPreset(QStringLiteral("k1"), QStringLiteral("Soft"), Tier::Mine,
                                   {application::ClashAction::Rename, u8"Gentle"}, true),
                 QStringLiteral("Gentle"));
        QCOMPARE(rowKeys(memory.presetRows(QStringLiteral("k1"))),
                 (QStringList{QStringLiteral("project:Soft"), QStringLiteral("project:Gentle"), QStringLiteral("mine:Own")}));
        QCOMPARE(memory.sendPreset(QStringLiteral("k1"), QStringLiteral("Own"), Tier::Mine, {}, false), QStringLiteral("Own"));
        QCOMPARE(rowKeys(memory.presetRows(QStringLiteral("k1"))),
                 (QStringList{QStringLiteral("project:Soft"), QStringLiteral("project:Gentle"), QStringLiteral("project:Own"),
                              QStringLiteral("mine:Own (hidden)")}));
        QVERIFY(!memory.sendPreset(QStringLiteral("k1"), QStringLiteral("Own"), Tier::Project, {}, false));
        QCOMPARE(memory.presetRows(QStringLiteral("k1")).size(), 4);

        memory.setProjectRoot({});
        QCOMPARE(rowKeys(memory.presetRows(QStringLiteral("k1"))), QStringList{QStringLiteral("mine:Own")});
    }
};

QTEST_GUILESS_MAIN(PresetTiersTests)
#include "preset_tiers_tests.moc"
