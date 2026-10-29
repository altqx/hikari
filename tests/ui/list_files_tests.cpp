// CF2 (#489): the user's lists as TOML files (list_files.h). Each legacy
// file converts once into its TOML file and reads back the values legacy
// read; an edit is saved and read back, keeping what this version does not
// know; a file that does not read is kept as <name>.broken and the list
// takes its defaults; a folder in the file's place, an unreadable legacy
// file and another HikariSub holding the folder's lock are no harm.

#include "hikari/app/settings_import_store.h"
#include "list_files.h"
#include "toml_file.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;
using application::FontCatalogs;
using application::ReplacerRule;
using application::visual::AllTagsSetting;
using application::visual::ShapePreset;
using ui::ListFile;

namespace {

namespace fs = std::filesystem;

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

fs::path pathOf(const QString &path)
{
    return fs::path(path.toStdU16String());
}

QString fixture(const char *name)
{
    return QStringLiteral(HIKARI_LIST_FIXTURES "/") + QLatin1String(name);
}

// The four lists: their files, the legacy fixture, and the legacy reader
// the app converts with.
enum class Kind { Rules, Shapes, Tags, FontCatalogs };

struct List {
    const char *file;
    const char *legacy;
    QString fixture;
    const ui::ListFormat &format;
    ListFile::Convert convert;
};

List listOf(Kind kind)
{
    switch (kind) {
    case Kind::Rules:
        return {"rules.toml", "Rules.txt", fixture("Rules.txt"), ui::kRulesFormat,
                [](const fs::path &legacy) -> std::optional<toml::array> {
                    if (const auto read = app::readLegacyRulesFile(legacy))
                        return ui::rulesToToml(read->rules);
                    return std::nullopt;
                }};
    case Kind::Shapes:
        return {"shapes.toml", "ShapesSettings.txt", fixture("ShapesSettings.txt"), ui::kShapesFormat,
                [](const fs::path &legacy) -> std::optional<toml::array> {
                    if (const auto shapes = ui::readLegacyShapes(legacy))
                        return ui::shapesToToml(*shapes);
                    return std::nullopt;
                }};
    case Kind::Tags:
        return {"tags.toml", "AllTagsSettings.txt", fixture("AllTagsSettings.txt"), ui::kTagsFormat,
                [](const fs::path &legacy) -> std::optional<toml::array> {
                    if (const auto tags = ui::readLegacyTags(legacy))
                        return ui::tagsToToml(*tags);
                    return std::nullopt;
                }};
    case Kind::FontCatalogs:
        break;
    }
    return {"fonts.toml", "FontCatalogs.txt", QStringLiteral(HIKARI_FONT_CATALOG_FIXTURES "/FontCatalogs.txt"),
            ui::kFontCatalogsFormat, [](const fs::path &legacy) -> std::optional<toml::array> {
                if (const auto catalogs = ui::readLegacyFontCatalogs(legacy))
                    return ui::fontCatalogsToToml(*catalogs);
                return std::nullopt;
            }};
}

// What a ListFile reported.
struct Reports {
    QStringList converted; // "<legacy> -> <file>"
    std::vector<application::FileProblem> problems;
    ListFile::Report report()
    {
        return {[this](const fs::path &legacy, const fs::path &file) {
                    converted << QString::fromStdU16String(legacy.filename().u16string()) + QStringLiteral(" -> ") +
                                     QString::fromStdU16String(file.filename().u16string());
                },
                [this](const application::FileProblem &problem) { problems.push_back(problem); }};
    }
};

std::shared_ptr<ListFile> open(const QTemporaryDir &dir, const List &list, Reports *reports = nullptr)
{
    return ListFile::make(dir.filePath(QLatin1String(list.file)), dir.filePath(QLatin1String(list.legacy)), list.format,
                          list.convert, reports ? reports->report() : ListFile::Report{});
}

// The values legacy read from the fixture, and what the TOML items read
// as, in one form to compare: each item's fields.
template <class Item, class From> std::vector<Item> valuesOf(const toml::array &items, From from)
{
    auto read = from(items);
    if (auto *values = std::get_if<std::vector<Item>>(&read))
        return *values;
    return {};
}

bool sameAsLegacy(Kind kind, const QString &legacyFile, const toml::array &items)
{
    const fs::path legacy = pathOf(legacyFile);
    switch (kind) {
    case Kind::Rules:
        return valuesOf<ReplacerRule>(items, ui::rulesFromToml) == app::readLegacyRulesFile(legacy)->rules;
    case Kind::Shapes:
        return valuesOf<ShapePreset>(items, ui::shapesFromToml) == *ui::readLegacyShapes(legacy);
    case Kind::Tags:
        return valuesOf<AllTagsSetting>(items, ui::tagsFromToml) == *ui::readLegacyTags(legacy);
    case Kind::FontCatalogs:
        break;
    }
    const auto catalogs = valuesOf<FontCatalogs::Catalog>(items, ui::fontCatalogsFromToml);
    const auto legacyCatalogs = *ui::readLegacyFontCatalogs(legacy);
    return std::ranges::equal(catalogs, legacyCatalogs, [](const auto &a, const auto &b) {
        return a.name == b.name && a.fonts == b.fonts;
    });
}

// An edit of each list: its first item changed, an item added.
toml::array edited(Kind kind, const toml::array &items)
{
    switch (kind) {
    case Kind::Rules: {
        auto rules = valuesOf<ReplacerRule>(items, ui::rulesFromToml);
        rules[0].checked = !rules[0].checked;
        rules[0].options = application::kReplacerMatchCase | application::kReplacerOnlyTags;
        rules.push_back({u8"Nowa reguła", u8"a+", u8"ą", application::kReplacerUpperCase, true});
        return ui::rulesToToml(rules);
    }
    case Kind::Shapes: {
        auto shapes = valuesOf<ShapePreset>(items, ui::shapesFromToml);
        shapes[0].mode = ShapePreset::OnlyScaleX;
        shapes.push_back({u"Trójkąt", u"m 0 0 l 50 100 100 0", ShapePreset::BothScaleX, ShapePreset::ChangeScale});
        return ui::shapesToToml(shapes);
    }
    case Kind::Tags: {
        auto tags = valuesOf<AllTagsSetting>(items, ui::tagsFromToml);
        tags[0].step = 0.25f;
        tags[0].mode = 2;
        auto added = AllTagsSetting::named(u"bord");
        added.values = {1.5f, 2.f, 0.f, 0.f};
        added.numOfValues = 2;
        tags.push_back(added);
        return ui::tagsToToml(tags);
    }
    case Kind::FontCatalogs:
        break;
    }
    auto catalogs = valuesOf<FontCatalogs::Catalog>(items, ui::fontCatalogsFromToml);
    catalogs[0].fonts.push_back(u"Zażółć Sans");
    catalogs.push_back({u"Nowy", {}});
    return ui::fontCatalogsToToml(catalogs);
}

} // namespace

class ListFilesTests : public QObject {
    Q_OBJECT

private slots:
    void eachList_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("rules") << int(Kind::Rules);
        QTest::newRow("shapes") << int(Kind::Shapes);
        QTest::newRow("tags") << int(Kind::Tags);
        QTest::newRow("font catalogs") << int(Kind::FontCatalogs);
    }

    // The legacy file converts once: the new file holds what legacy read,
    // the old one is kept as <name>.old byte for byte and never written.
    void legacyFileConvertsOnce_data() { eachList_data(); }
    void legacyFileConvertsOnce()
    {
        QFETCH(int, kind);
        const List list = listOf(Kind(kind));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QLatin1String(list.legacy));
        const QByteArray bytes = readAll(list.fixture);
        QVERIFY(!bytes.isEmpty());
        QVERIFY(writeAll(legacy, bytes));
        // The values legacy read from it, before it is moved.
        QVERIFY(QFile::copy(legacy, dir.filePath(QStringLiteral("expected"))));

        Reports reports;
        auto file = open(dir, list, &reports);
        const auto items = file->read();
        QVERIFY(items);
        QCOMPARE(reports.converted, QStringList{QLatin1String(list.legacy) + QStringLiteral(" -> ") + QLatin1String(list.file)});
        QVERIFY(reports.problems.empty());
        QVERIFY(!items->empty());
        QVERIFY(sameAsLegacy(Kind(kind), dir.filePath(QStringLiteral("expected")), *items));
        QVERIFY(!QFileInfo::exists(legacy));
        QCOMPARE(readAll(legacy + QStringLiteral(".old")), bytes);
        QVERIFY(readAll(dir.filePath(QLatin1String(list.file))).startsWith("version = 1\n"));

        // The next start reads the TOML file, and a legacy file put back is
        // left alone.
        QVERIFY(writeAll(legacy, "stale"));
        Reports again;
        auto next = open(dir, list, &again);
        const auto reread = next->read();
        QVERIFY(reread);
        QVERIFY(again.converted.isEmpty());
        QVERIFY(sameAsLegacy(Kind(kind), dir.filePath(QStringLiteral("expected")), *reread));
        QCOMPARE(readAll(legacy), QByteArray("stale"));
    }

    // An edit written and read back; the keys this version does not know
    // stay (the file's own above the list, an item's on the item of its
    // name), and a known option turned off stays off.
    void editsRoundTripKeepingUnknownKeys_data() { eachList_data(); }
    void editsRoundTripKeepingUnknownKeys()
    {
        QFETCH(int, kind);
        const List list = listOf(Kind(kind));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QFile::copy(list.fixture, dir.filePath(QLatin1String(list.legacy))));
        auto file = open(dir, list);
        auto items = file->read();
        QVERIFY(items);

        // A later version's keys, put in by hand.
        const QString path = dir.filePath(QLatin1String(list.file));
        auto parsed = ui::parseTomlFile(readAll(path).toStdString(), ListFile::kVersion);
        auto table = std::get<toml::table>(parsed);
        table.insert("later", "kept");
        table[list.format.array].as_array()->get(0)->as_table()->insert("colour", "red");
        QVERIFY(writeAll(path, QByteArray::fromStdString(ui::formatTomlFile(table, ListFile::kVersion))));

        const toml::array changed = edited(Kind(kind), *items);
        QCOMPARE(file->write(changed), ListFile::Write::Written);
        const auto back = open(dir, list)->read();
        QVERIFY(back);
        QCOMPARE(back->size(), changed.size());
        QVERIFY(QByteArray::fromStdString(ui::listFileText(list.format, *back)).contains("colour = \"red\""));
        const QByteArray text = readAll(path);
        QVERIFY2(text.startsWith("version = 1\n\nlater = \"kept\"\n\n["), text.constData());
        // The written list is the edit: its items with the unknown key aside.
        auto withoutColour = *back;
        withoutColour.get(0)->as_table()->erase("colour");
        QCOMPARE(QByteArray::fromStdString(ui::listFileText(list.format, withoutColour)),
                 QByteArray::fromStdString(ui::listFileText(list.format, changed)));
    }

    // A file that does not read is kept as <name>.broken (then
    // <name>.broken-2), reported where it fails, and the list takes its
    // defaults until it is written again.
    void brokenFileIsKeptAside_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<int>("line");
        QTest::newRow("rules: syntax") << int(Kind::Rules) << QByteArray("version = 1\n[[rule]\n") << 2;
        QTest::newRow("rules: a flag as text") << int(Kind::Rules)
                                               << QByteArray("version = 1\n\n[[rule]]\nfind = \"a\"\nmatchCase = \"yes\"\n")
                                               << 5;
        QTest::newRow("shapes: unknown scaling") << int(Kind::Shapes)
                                                 << QByteArray("version = 1\n[[shape]]\nname = \"a\"\nscaling = \"big\"\n")
                                                 << 4;
        QTest::newRow("shapes: mode past a byte") << int(Kind::Shapes)
                                                  << QByteArray("version = 1\n[[shape]]\nfromCursor = 256\n") << 3;
        QTest::newRow("tags: five values") << int(Kind::Tags)
                                           << QByteArray("version = 1\n[[tag]]\nvalues = [1, 2, 3, 4, 5]\n") << 3;
        QTest::newRow("tags: a number as text") << int(Kind::Tags) << QByteArray("version = 1\n[[tag]]\nmin = \"0\"\n") << 3;
        QTest::newRow("catalogs: not a list") << int(Kind::FontCatalogs) << QByteArray("version = 1\ncatalog = 3\n") << 2;
        QTest::newRow("catalogs: a font as a number")
            << int(Kind::FontCatalogs) << QByteArray("version = 1\n[[catalog]]\nname = \"A\"\nfonts = [\"Arial\", 3]\n") << 4;
        QTest::newRow("newer version") << int(Kind::Shapes) << QByteArray("version = 2\n") << 0;
        QTest::newRow("no version") << int(Kind::Rules) << QByteArray("[[rule]]\n") << 0;
    }
    void brokenFileIsKeptAside()
    {
        QFETCH(int, kind);
        QFETCH(QByteArray, text);
        QFETCH(int, line);
        const List list = listOf(Kind(kind));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QLatin1String(list.file));
        QVERIFY(writeAll(path, text));
        QVERIFY(writeAll(path + QStringLiteral(".broken"), "an earlier one"));
        Reports reports;
        auto file = open(dir, list, &reports);
        QVERIFY(!file->read());
        QCOMPARE(reports.problems.size(), std::size_t(1));
        const auto &problem = reports.problems[0];
        QVERIFY(problem.file == pathOf(path));
        QVERIFY(problem.keptAs == pathOf(path + QStringLiteral(".broken-2")));
        QCOMPARE(problem.failure.line, line);
        QVERIFY(!problem.failure.message.empty());
        QCOMPARE(readAll(path + QStringLiteral(".broken-2")), text);
        QCOMPARE(readAll(path + QStringLiteral(".broken")), QByteArray("an earlier one"));
        QVERIFY(!QFileInfo::exists(path));
        // The next save writes a good file.
        QCOMPARE(file->write(toml::array()), ListFile::Write::Written);
        QCOMPARE(readAll(path), QByteArray("version = 1\n\n" + QByteArray(list.format.array.data(),
                                                                        qsizetype(list.format.array.size())) +
                                           " = []\n"));
    }

    // A folder where the file should be cannot be read or moved: the
    // defaults, said once, and nothing is written over it.
    void folderInTheFilesPlace_data() { eachList_data(); }
    void folderInTheFilesPlace()
    {
        QFETCH(int, kind);
        const List list = listOf(Kind(kind));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QLatin1String(list.file));
        QVERIFY(QDir().mkpath(path));
        Reports reports;
        auto file = open(dir, list, &reports);
        QVERIFY(!file->read());
        QVERIFY(!file->read());
        QCOMPARE(reports.problems.size(), std::size_t(1));
        QVERIFY(reports.problems[0].keptAs.empty());
        QCOMPARE(file->write(toml::array()), ListFile::Write::Failed);
        QVERIFY(QFileInfo(path).isDir());
    }

    // A legacy file that cannot be read is not converted or renamed, and
    // the list is not written this session: the next start tries again.
    void unreadableLegacyFileIsLeft_data() { eachList_data(); }
    void unreadableLegacyFileIsLeft()
    {
        QFETCH(int, kind);
        const List list = listOf(Kind(kind));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QLatin1String(list.legacy));
        QVERIFY(QFile::copy(list.fixture, legacy));
        // Windows and root keep read access whatever setPermissions asks.
        if (QFile probe(legacy); !QFile::setPermissions(legacy, {}) || probe.open(QIODevice::ReadOnly)) {
            QFile::setPermissions(legacy, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            QSKIP("the file reads without permissions (Windows, root, or a file system without them)");
        }
        Reports reports;
        auto file = open(dir, list, &reports);
        QVERIFY(!file->read());
        QVERIFY(!file->read());
        QCOMPARE(reports.problems.size(), std::size_t(1));
        QVERIFY(reports.problems[0].file == pathOf(legacy));
        QVERIFY(reports.converted.isEmpty());
        QCOMPARE(file->write(toml::array()), ListFile::Write::Failed);
        QVERIFY(QFile::setPermissions(legacy, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(!QFileInfo::exists(dir.filePath(QLatin1String(list.file))));
        QVERIFY(!QFileInfo::exists(legacy + QStringLiteral(".old")));
        QVERIFY(open(dir, list)->read());
        QVERIFY(QFileInfo::exists(legacy + QStringLiteral(".old")));
    }

    // The converted file is as private as the legacy one; a rewrite keeps
    // the file's permissions.
    void permissionsCarryOver()
    {
#ifdef _WIN32
        QSKIP("no POSIX permissions");
#else
        const List list = listOf(Kind::Rules);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QLatin1String(list.legacy));
        QVERIFY(QFile::copy(list.fixture, legacy));
        const auto owner = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
        // The owner's, the group's and the others' (not the user's, which
        // repeat the owner's here).
        const auto mode = [](QFileDevice::Permissions p) { return p.toInt() & 0x7077; };
        QVERIFY(QFile::setPermissions(legacy, owner));
        auto file = open(dir, list);
        const auto items = file->read();
        QVERIFY(items);
        const QString path = dir.filePath(QLatin1String(list.file));
        QCOMPARE(mode(QFile::permissions(path)), mode(owner));
        QVERIFY(QFile::setPermissions(path, owner | QFileDevice::ReadGroup));
        QCOMPARE(file->write(*items), ListFile::Write::Written);
        QCOMPARE(mode(QFile::permissions(path)), mode(owner | QFileDevice::ReadGroup));
#endif
    }

    // While another HikariSub holds the folder's lock the list waits, and
    // is written once the lock is free.
    void lockedFolderIsWrittenLater()
    {
        const List list = listOf(Kind::Shapes);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto file = open(dir, list);
        QLockFile other(dir.filePath(QStringLiteral(".settings.lock")));
        QVERIFY(other.tryLock(0));
        const auto shapes = ui::shapesToToml(application::visual::defaultShapePresets());
        QCOMPARE(file->write(shapes), ListFile::Write::Waiting);
        const QString path = dir.filePath(QLatin1String(list.file));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!file->flush());
        other.unlock();
        QTRY_VERIFY(QFileInfo::exists(path));
        QVERIFY(file->flush());
        QCOMPARE(open(dir, list)->read()->size(), shapes.size());
    }

    // The fields as they are written: named choices, a choice legacy could
    // hold without a name as its number, floats in their shortest form.
    void fieldsAreNamed()
    {
        const List shapes = listOf(Kind::Shapes);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QFile::copy(shapes.fixture, dir.filePath(QLatin1String(shapes.legacy))));
        QVERIFY(open(dir, shapes)->read());
        const QByteArray shapesText = readAll(dir.filePath(QLatin1String(shapes.file)));
        QVERIFY2(shapesText.contains("[[shape]]\ndrawing = \"m 0 0 l 100 0 100 100 0 100\"\nfromCursor = "
                                     "\"widthAndHeight\"\nname = \"box\"\nscaling = \"points\"\n"),
                 shapesText.constData());
        QVERIFY(shapesText.contains("fromCursor = \"keepAspectRatio\"\nname = \"Kółko\"\nscaling = \"scale\""));
        QVERIFY(shapesText.contains("fromCursor = 7\nname = \"odd\""));

        const List tags = listOf(Kind::Tags);
        QVERIFY(QFile::copy(tags.fixture, dir.filePath(QLatin1String(tags.legacy))));
        QVERIFY(open(dir, tags)->read());
        const QByteArray tagsText = readAll(dir.filePath(QLatin1String(tags.file)));
        QVERIFY2(tagsText.contains("[[tag]]\nchange = \"set\"\ndecimals = 1\nmax = 100.0\nmin = 0.0\nname = \"blur\"\n"
                                   "placing = \"cursor\"\nstep = 0.5\ntag = \"blur\"\nvalues = [ 0.0 ]\n"),
                 tagsText.constData());
        QVERIFY(tagsText.contains("values = [ 255.0, 128.0, 64.0 ]"));
        QVERIFY(tagsText.contains("change = \"gradientLinesDown\""));
        QVERIFY(tagsText.contains("placing = \"clipAnimation\""));
        QVERIFY(tagsText.contains("step = 0.1"));

        const List rules = listOf(Kind::Rules);
        QVERIFY(QFile::copy(rules.fixture, dir.filePath(QLatin1String(rules.legacy))));
        QVERIFY(open(dir, rules)->read());
        const QByteArray rulesText = readAll(dir.filePath(QLatin1String(rules.file)));
        QVERIFY2(rulesText.contains("[[rule]]\ndescription = \"Tags only, lower\"\nenabled = true\nfind = \"\\\\\\\\FS\"\n"
                                    "lowerCase = true\nonlyInTags = true\nonlyInText = true\nreplace = \"\\\\\\\\fs\"\n"),
                 rulesText.constData());
    }

    // The font catalog window's Load reads a fonts.toml (or an autosave)
    // as one; other text is legacy's.
    void fontCatalogsFromText()
    {
        const auto toml = ui::fontCatalogsFromText("version = 1\n[[catalog]]\nname = \"A\"\nfonts = [\"Arial\", \"Arial\", \"Ż\"]\n");
        QVERIFY(toml);
        QCOMPARE(toml->size(), std::size_t(1));
        QVERIFY((*toml)[0].name == u"A");
        QVERIFY(((*toml)[0].fonts == std::vector<std::u16string>{u"Arial", u"Ż"}));
        QVERIFY(ui::fontCatalogsFromText("version = 1\n")->empty());
        QVERIFY(!ui::fontCatalogsFromText(readAll(listOf(Kind::FontCatalogs).fixture).toStdString()));
        QVERIFY(!ui::fontCatalogsFromText("version = 2\n"));
        QVERIFY(!ui::fontCatalogsFromText("version = 1\n[[catalog]]\nfonts = 3\n"));
    }
};

QTEST_GUILESS_MAIN(ListFilesTests)
#include "list_files_tests.moc"
