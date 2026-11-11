#include "project_credentials.h"
#include "project_file.h"
#include "toml_file.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;
using application::SettingValue;
using hikari::ui::ProjectCredentials;
using hikari::ui::ProjectFile;
using hikari::ui::patchTomlSetting;

namespace {

const application::SettingDefinition &setting(const char *id)
{
    return *application::findSetting(id);
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

const char kHandWritten[] = "version = 1\n"
                            "# Shared by the team; keep en_GB.\n"
                            "\n"
                            "[editor]\n"
                            "dictionaryLanguage = \"en_GB\" # the show is British\n"
                            "spellchecker = true\n"
                            "\n"
                            "# Grammar follows the dictionary.\n"
                            "[grammar]\n"
                            "language = 2\n"
                            "unknownKey = [\n"
                            "  \"kept\", # inside an array\n"
                            "]\n";

}

class ProjectFileTests : public QObject {
    Q_OBJECT

private slots:
    void aPatchChangesOnlyItsValue()
    {
        const auto patched = patchTomlSetting(kHandWritten, "grammar.language", SettingValue(std::int64_t{3}));
        QVERIFY(patched);
        std::string expected = kHandWritten;
        expected.replace(expected.find("language = 2"), 12, "language = 3");
        QCOMPARE(*patched, expected);
    }

    void aPatchKeepsATrailingComment()
    {
        const auto patched =
            patchTomlSetting(kHandWritten, "editor.dictionaryLanguage", SettingValue(std::string("pl")));
        QVERIFY(patched);
        QVERIFY(patched->find("dictionaryLanguage = \"pl\" # the show is British\n") != std::string::npos);
        QCOMPARE(patched->size(), std::string(kHandWritten).size() - 3);
    }

    void aNewKeyJoinsTheEndOfItsTable()
    {
        const auto patched =
            patchTomlSetting(kHandWritten, "grammar.group.properNouns", SettingValue(false));
        QVERIFY(patched);
        QVERIFY(patched->starts_with(kHandWritten));
        QCOMPARE(patched->substr(std::string(kHandWritten).size()), std::string("\n[grammar.group]\nproperNouns = false\n"));

        const auto inEditor =
            patchTomlSetting(kHandWritten, "editor.spellchecker", std::nullopt);
        QVERIFY(inEditor);
        std::string expected = kHandWritten;
        expected.erase(expected.find("spellchecker = true\n"), 20);
        QCOMPARE(*inEditor, expected);

        const auto added = patchTomlSetting(*inEditor, "editor.spellchecker", SettingValue(false));
        QVERIFY(added);
        expected.insert(expected.find("\n# Grammar"), "spellchecker = false\n");
        QCOMPARE(*added, expected);
    }

    void aKeyWrittenWithDotsIsPatchedInPlace()
    {
        const std::string text = "version = 1\neditor.dictionaryLanguage = \"de\"\n[grammar]\nlanguage = 1\n";
        const auto patched = patchTomlSetting(text, "editor.dictionaryLanguage", SettingValue(std::string("fr")));
        QVERIFY(patched);
        QCOMPARE(*patched, std::string("version = 1\neditor.dictionaryLanguage = \"fr\"\n[grammar]\nlanguage = 1\n"));
    }

    void anInlineTableCantBePatchedInPlace()
    {
        const std::string text = "version = 1\neditor = { spellchecker = true }\n";
        QVERIFY(!patchTomlSetting(text, "editor.spellchecker", SettingValue(false)));
        QVERIFY(!patchTomlSetting(text, "editor.dictionaryLanguage", SettingValue(std::string("x"))));
    }

    void theLocalFileWinsOverTheSharedOne()
    {
        QTemporaryDir root;
        writeAll(root.filePath(QStringLiteral(".hikari/project.toml")), kHandWritten);
        writeAll(root.filePath(QStringLiteral(".hikari/project.local.toml")),
                 "version = 1\n[editor]\ndictionaryLanguage = \"en_US\"\n");
        const ProjectFile project(root.path());
        QVERIFY(project.problems().empty());
        QCOMPARE(project.read(setting("editor.dictionaryLanguage")), SettingValue(std::string("en_US")));
        QCOMPARE(project.source(setting("editor.dictionaryLanguage")), ProjectFile::Local);
        QCOMPARE(project.read(setting("grammar.language")), SettingValue(std::int64_t{2}));
        QCOMPARE(project.source(setting("grammar.language")), ProjectFile::Shared);
        QVERIFY(!project.read(setting("translation.autoMoveTagsFromOriginal")));
    }

    void fontFoldersAreRelativeToTheProjectFolder()
    {
        QTemporaryDir root;
        writeAll(root.filePath(QStringLiteral(".hikari/project.toml")),
                 "version = 1\nfonts = [\"fonts\", \"../common/fonts\", \"\"]\n");
        const ProjectFile project(root.path());
        QVERIFY(project.problems().empty());
        QVERIFY(project.refused().empty());
        const QDir folder(root.path());
        QCOMPARE(project.fontFolders(), (std::vector<QString>{QDir::cleanPath(folder.filePath(QStringLiteral("fonts"))),
                                                              QDir::cleanPath(folder.filePath(QStringLiteral("../common/fonts")))}));

        writeAll(root.filePath(QStringLiteral(".hikari/project.local.toml")), "version = 1\nfonts = [\"mine\"]\n");
        QCOMPARE(ProjectFile(root.path()).fontFolders(),
                 std::vector<QString>{QDir::cleanPath(folder.filePath(QStringLiteral("mine")))});
        QVERIFY(ProjectFile(QTemporaryDir().path()).fontFolders().empty());
    }

    void settingsOffTheAllowlistAreRefusedAndKept()
    {
        QTemporaryDir root;
        const QByteArray text = "version = 1\n[privacy]\nusageData = false\n[program]\ntheme = \"Light\"\n"
                                "[editor]\nspellchecker = false\n";
        writeAll(root.filePath(QStringLiteral(".hikari/project.toml")), text);
        ProjectFile project(root.path());
        QVERIFY(!project.read(setting("privacy.usageData")));
        QVERIFY(!project.read(setting("program.theme")));
        QCOMPARE(project.refused(), (std::vector<std::string>{"privacy.usageData", "program.theme"}));

        project.write(setting("privacy.usageData"), true);
        QVERIFY(project.lastWriteFailed());
        project.write(setting("editor.spellchecker"), true);
        QVERIFY(!project.lastWriteFailed());
        QCOMPARE(readAll(project.path(ProjectFile::Shared)),
                 QByteArray("version = 1\n[privacy]\nusageData = false\n[program]\ntheme = \"Light\"\n"
                            "[editor]\nspellchecker = true\n"));
    }

    void theFirstProjectSettingCreatesTheFile()
    {
        QTemporaryDir root;
        ProjectFile project(root.path());
        project.remove(setting("editor.spellchecker"));
        QVERIFY(!QDir(root.filePath(QStringLiteral(".hikari"))).exists());

        project.write(setting("editor.dictionaryLanguage"), std::string("en_US"));
        QCOMPARE(readAll(project.path(ProjectFile::Shared)),
                 QByteArray("version = 1\n\n[editor]\ndictionaryLanguage = \"en_US\"\n"));
        QCOMPARE(project.read(setting("editor.dictionaryLanguage")), SettingValue(std::string("en_US")));
        QVERIFY(!QFile::exists(project.path(ProjectFile::Local)));
    }

    void writesNeverTouchTheLocalFile()
    {
        QTemporaryDir root;
        const QByteArray local = "version = 1\n# mine\n[editor]\ndictionaryLanguage = \"pl\"\n";
        writeAll(root.filePath(QStringLiteral(".hikari/project.local.toml")), local);
        ProjectFile project(root.path());
        project.write(setting("editor.dictionaryLanguage"), std::string("de_DE"));
        project.remove(setting("editor.dictionaryLanguage"));
        QCOMPARE(readAll(project.path(ProjectFile::Local)), local);
        QCOMPARE(project.read(setting("editor.dictionaryLanguage")), SettingValue(std::string("pl")));
    }

    void anInlineTableFallsBackToAFullWrite()
    {
        QTemporaryDir root;
        writeAll(root.filePath(QStringLiteral(".hikari/project.toml")), "version = 1\neditor = { spellchecker = true }\n");
        ProjectFile project(root.path());
        project.write(setting("editor.spellchecker"), false);
        QVERIFY(!project.lastWriteFailed());
        QCOMPARE(ProjectFile(root.path()).read(setting("editor.spellchecker")), SettingValue(false));
    }

    void aBrokenFileIsKeptAsBrokenAndNotRead()
    {
        QTemporaryDir root;
        const QByteArray broken = "version = 1\n[editor]\nspellchecker = = false\n";
        writeAll(root.filePath(QStringLiteral(".hikari/project.toml")), broken);
        ProjectFile project(root.path());
        QCOMPARE(project.problems().size(), std::size_t{1});
        QCOMPARE(readAll(root.filePath(QStringLiteral(".hikari/project.toml.broken"))), broken);
        QVERIFY(!QFile::exists(project.path(ProjectFile::Shared)));
        QVERIFY(!project.read(setting("editor.spellchecker")));

        project.write(setting("editor.spellchecker"), false);
        QVERIFY(!project.lastWriteFailed());
        QCOMPARE(readAll(project.path(ProjectFile::Shared)), QByteArray("version = 1\n\n[editor]\nspellchecker = false\n"));
    }

    void aNewerVersionIsBroken()
    {
        QTemporaryDir root;
        writeAll(root.filePath(QStringLiteral(".hikari/project.local.toml")), "version = 2\n");
        const ProjectFile project(root.path());
        QCOMPARE(project.problems().size(), std::size_t{1});
        QVERIFY(QFile::exists(root.filePath(QStringLiteral(".hikari/project.local.toml.broken"))));
    }

    void aiKeysNameTheirKeysAndTheLocalFileWins()
    {
        QTemporaryDir root;
        writeAll(root.path() + "/.hikari/project.toml", "version = 1\n[ai.keys]\n"
                                                         "openai = { env = \"OPENAI_API_KEY\", entry = \"openai\" }\n"
                                                         "anthropic = { entry = \"anthropic\" }\n");
        writeAll(root.path() + "/.hikari/project.local.toml", "version = 1\n[ai.keys]\n"
                                                               "anthropic = { env = \"MY_ANTHROPIC\" }\n");
        const auto names = ProjectFile(root.path()).aiKeyNames();
        QCOMPARE(names.byProvider.size(), std::size_t(2));
        QVERIFY((names.byProvider.at("openai") == application::ai::KeyName{"OPENAI_API_KEY", "openai"}));
        QVERIFY((names.byProvider.at("anthropic") == application::ai::KeyName{"MY_ANTHROPIC", ""}));
        QVERIFY(names.valuesFound.empty());
    }

    void aKeyValueInProjectTomlIsIgnoredAndReported()
    {
        QTemporaryDir root;
        writeAll(root.path() + "/.hikari/project.toml", "version = 1\n[ai.keys]\n"
                                                         "openrouter = \"sk-or-secret\"\n"
                                                         "openai = { key = \"sk-secret\" }\n"
                                                         "[keys]\nanthropic = \"sk-ant-secret\"\n");
        const ProjectFile project(root.path());
        const auto names = project.aiKeyNames();
        QVERIFY(names.byProvider.empty());
        QCOMPARE(names.valuesFound, (std::vector<std::string>{"openai", "openrouter", "keys"}));
        const auto values = project.aiKeyValues();
        QCOMPARE(values.size(), std::size_t(3));
        QCOMPARE(values[0].key, std::string("sk-secret"));
        QCOMPARE(values[1].key, std::string("sk-or-secret"));
        QVERIFY(values[2].inKeysTable);
        QVERIFY(project.refused().empty());
    }

    void settingAKeyNameKeepsTheRestOfTheFile()
    {
        QTemporaryDir root;
        const QString path = root.path() + "/.hikari/project.toml";
        writeAll(path, kHandWritten);
        ProjectFile project(root.path());
        QVERIFY(project.setAiKeyName("openai", {"OPENAI_API_KEY", ""}));
        const QByteArray written = readAll(path);
        QVERIFY(written.startsWith(kHandWritten));
        QVERIFY(written.contains("openai = { env = \"OPENAI_API_KEY\" }"));
        QVERIFY((ProjectFile(root.path()).aiKeyNames().byProvider.at("openai").env == "OPENAI_API_KEY"));
        QVERIFY(project.setAiKeyName("openai", {}));
        QVERIFY(ProjectFile(root.path()).aiKeyNames().byProvider.empty());
        QVERIFY(readAll(path).contains("# the show is British"));
    }

    void creatingCredentialsWritesTheFileAndTheGitignore()
    {
        QTemporaryDir root;
        const ProjectCredentials credentials(root.path());
        QVERIFY(!credentials.exists());
        QVERIFY(credentials.create({"anthropic"}));
        QVERIFY(credentials.exists());
        QCOMPARE(readAll(credentials.gitignorePath()), QByteArray("credentials.toml\nproject.local.toml\n"));
        const auto keys = credentials.keys();
        QCOMPARE(keys.size(), std::size_t(1));
        QCOMPARE(keys.at("anthropic"), std::string());
        QVERIFY(!QFileInfo::exists(root.path() + "/.gitignore"));
#ifndef Q_OS_WIN
        QCOMPARE(QFile::permissions(credentials.path()) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
#endif
    }

    void anExistingGitignoreGainsOnlyTheMissingLines()
    {
        QTemporaryDir root;
        const ProjectCredentials credentials(root.path());
        writeAll(credentials.gitignorePath(), "layouts.json\r\n/project.local.toml");
        QVERIFY(credentials.create({}));
        QCOMPARE(readAll(credentials.gitignorePath()), QByteArray("layouts.json\r\n/project.local.toml\ncredentials.toml\n"));
        QVERIFY(credentials.create({}));
        QCOMPARE(readAll(credentials.gitignorePath()), QByteArray("layouts.json\r\n/project.local.toml\ncredentials.toml\n"));
    }

    void creatingKeepsAnExistingCredentialsFile()
    {
        QTemporaryDir root;
        const ProjectCredentials credentials(root.path());
        writeAll(credentials.path(), "version = 1\n[keys]\nopenai = \"kept\"\n");
        QVERIFY(credentials.create({"anthropic"}));
        QCOMPARE(credentials.keys(), (ProjectCredentials::Keys{{"openai", "kept"}}));
    }

    void puttingAKeyCreatesTheFileAndKeepsItPrivate()
    {
        QTemporaryDir root;
        const ProjectCredentials credentials(root.path());
        QVERIFY(credentials.put("openrouter", "sk-or-moved"));
        QVERIFY(credentials.put("openai", "sk-new"));
        QVERIFY(credentials.put("openrouter", "sk-or-again"));
        QCOMPARE(credentials.keys(), (ProjectCredentials::Keys{{"openai", "sk-new"}, {"openrouter", "sk-or-again"}}));
        QVERIFY(readAll(credentials.path()).startsWith("# AI keys for this project."));
#ifndef Q_OS_WIN
        QCOMPARE(QFile::permissions(credentials.path()) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
#endif
    }

    void aBrokenCredentialsFileHasNoKeys()
    {
        QTemporaryDir root;
        const ProjectCredentials credentials(root.path());
        writeAll(credentials.path(), "version = 1\n[keys\n");
        std::optional<application::ParseFailure> problem;
        QVERIFY(credentials.keys(&problem).empty());
        QVERIFY(problem);
    }
};

QTEST_GUILESS_MAIN(ProjectFileTests)
#include "project_file_tests.moc"
