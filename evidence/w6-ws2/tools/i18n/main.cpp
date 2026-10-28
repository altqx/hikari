// hikari_i18n_migrate: the reproducible O4 conversion of the legacy PO
// catalogs into TS (docs/qt/localisation.md). Developer-invoked through the
// hikari_i18n_keymap and hikari_i18n_convert targets; ordinary builds only run
// its probe subcommand for the plural tests.
//
//   keymap  --pot <template.pot> --keys <keys.ts> --commit <sha> --out <keymap.tsv>
//   convert --map <keymap.tsv> --keys <keys.ts> --pot <template.pot>
//           --po <language>=<file.po>... --out-dir <dir> --report <report.md>
//   probe   --po <file.po|template.pot> --language <language> --out <file.ts>
//
// O6, semantic contexts (i18n/contexts.tsv):
//   lint       --contexts <contexts.tsv> --extracted <located.ts> --sources <dir>
//   contextmap --before <located.ts> --after <located.ts> [--catalog <file.ts>]... --out <contexts.tsv>
//   recontext  --map <contexts.tsv> --ts <file.ts>...          (rewrites each in place)
//   compare    --map <contexts.tsv> --before <file.ts> --after <file.ts>

#include "hikari/i18n/contexts.h"
#include "hikari/i18n/migration.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMultiMap>

#include <algorithm>
#include <cstdio>

using namespace hikari::i18n;

namespace {

int fail(const QString &message)
{
    std::fprintf(stderr, "hikari_i18n_migrate: %s\n", qPrintable(message));
    return 1;
}

std::expected<QByteArray, QString> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("cannot read %1: %2").arg(path, file.errorString()));
    return file.readAll();
}

// Leaves an unchanged file alone, so nothing downstream rebuilds.
std::expected<void, QString> writeFile(const QString &path, const QByteArray &bytes)
{
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes)
        return {};
    existing.close();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
        return std::unexpected(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    return {};
}

std::expected<TsCatalog, QString> readKeys(const QString &path)
{
    auto bytes = readFile(path);
    if (!bytes)
        return std::unexpected(bytes.error());
    auto keys = readTs(*bytes);
    if (!keys)
        return std::unexpected(QStringLiteral("%1: %2").arg(path, keys.error()));
    return keys;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (args.isEmpty())
        return fail(QStringLiteral("usage: hikari_i18n_migrate keymap|convert|probe|lint|contextmap|recontext|compare [options]"));
    const QString command = args.takeFirst();
    QMultiMap<QString, QString> options;
    for (qsizetype i = 0; i < args.size(); i += 2) {
        if (!args[i].startsWith(QStringLiteral("--")) || i + 1 >= args.size())
            return fail(QStringLiteral("expected --option value pairs, got %1").arg(args[i]));
        options.insert(args[i].mid(2), args[i + 1]);
    }
    const auto option = [&](const QString &name) { return options.value(name); };
    for (const QString &name : options.uniqueKeys())
        if (name != u"po" && name != u"catalog" && name != u"ts" && options.count(name) > 1)
            return fail(QStringLiteral("--%1 given twice").arg(name));

    if (command == u"keymap") {
        auto pot = readFile(option(QStringLiteral("pot")));
        if (!pot)
            return fail(pot.error());
        auto legacy = parsePo(*pot);
        if (!legacy)
            return fail(QStringLiteral("template: %1").arg(legacy.error()));
        auto keys = readKeys(option(QStringLiteral("keys")));
        if (!keys)
            return fail(keys.error());
        if (option(QStringLiteral("commit")).isEmpty())
            return fail(QStringLiteral("--commit is required"));
        const KeyMap map = buildKeyMap(*legacy, *keys, option(QStringLiteral("commit")));
        if (auto written = writeFile(option(QStringLiteral("out")), writeKeyMap(map)); !written)
            return fail(written.error());
        return 0;
    }

    if (command == u"convert") {
        auto mapBytes = readFile(option(QStringLiteral("map")));
        if (!mapBytes)
            return fail(mapBytes.error());
        auto map = readKeyMap(*mapBytes);
        if (!map)
            return fail(QStringLiteral("key map: %1").arg(map.error()));
        auto keys = readKeys(option(QStringLiteral("keys")));
        if (!keys)
            return fail(keys.error());
        auto pot = readFile(option(QStringLiteral("pot")));
        if (!pot)
            return fail(pot.error());
        QList<LanguageInput> languages;
        for (const QString &spec : options.values(QStringLiteral("po"))) {
            const qsizetype eq = spec.indexOf(u'=');
            if (eq <= 0)
                return fail(QStringLiteral("--po expects <language>=<file>, got %1").arg(spec));
            auto bytes = readFile(spec.mid(eq + 1));
            if (!bytes)
                return fail(bytes.error());
            languages.append({spec.left(eq), QFileInfo(spec.mid(eq + 1)).fileName(), *bytes});
        }
        // QMultiMap returns the values newest first.
        std::reverse(languages.begin(), languages.end());
        auto output = migrate(*map, *keys, *pot, languages);
        if (!output)
            return fail(output.error());
        const QDir dir(option(QStringLiteral("out-dir")));
        for (auto it = output->uiCatalogs.cbegin(); it != output->uiCatalogs.cend(); ++it)
            if (auto written = writeFile(dir.filePath(QStringLiteral("hikarisub_%1.ts").arg(it.key())), it.value()); !written)
                return fail(written.error());
        for (auto it = output->gettextCatalogs.cbegin(); it != output->gettextCatalogs.cend(); ++it)
            if (auto written = writeFile(dir.filePath(QStringLiteral("hikarisub_gettext_%1.ts").arg(it.key())), it.value());
                !written)
                return fail(written.error());
        if (auto written = writeFile(option(QStringLiteral("report")), output->report); !written)
            return fail(written.error());
        return 0;
    }

    if (command == u"probe") {
        const QString path = option(QStringLiteral("po"));
        auto bytes = readFile(path);
        if (!bytes)
            return fail(bytes.error());
        auto probe = legacyPluralProbe({option(QStringLiteral("language")), QFileInfo(path).fileName(), *bytes});
        if (!probe)
            return fail(probe.error());
        if (auto written = writeFile(option(QStringLiteral("out")), *probe); !written)
            return fail(written.error());
        return 0;
    }
    if (command == u"lint") {
        auto registryBytes = readFile(option(QStringLiteral("contexts")));
        if (!registryBytes)
            return fail(registryBytes.error());
        auto registry = readContextRegistry(*registryBytes);
        if (!registry)
            return fail(QStringLiteral("%1: %2").arg(option(QStringLiteral("contexts")), registry.error()));
        auto extracted = readKeys(option(QStringLiteral("extracted")));
        if (!extracted)
            return fail(extracted.error());
        // Locations are relative to the TS file; report them from the sources.
        const QDir tsDir = QFileInfo(option(QStringLiteral("extracted"))).absoluteDir();
        const QDir sourceDir(option(QStringLiteral("sources")));
        int messages = 0;
        for (TsContext &context : extracted->contexts)
            for (TsMessage &m : context.messages) {
                ++messages;
                for (auto &location : m.locations)
                    location.first = QDir(sourceDir.absolutePath() + QStringLiteral("/..")).relativeFilePath(
                        QDir::cleanPath(tsDir.absoluteFilePath(location.first)));
            }
        QList<SourceFile> sources;
        QDirIterator it(sourceDir.absolutePath(), {QStringLiteral("*.qml"), QStringLiteral("*.js"), QStringLiteral("*.mjs"),
                                                   QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            auto bytes = readFile(path);
            if (!bytes)
                return fail(bytes.error());
            sources.append({QDir(sourceDir.absolutePath() + QStringLiteral("/..")).relativeFilePath(path),
                            QString::fromUtf8(*bytes)});
        }
        std::ranges::sort(sources, {}, &SourceFile::path);
        const QStringList problems = lintContexts(*registry, *extracted, sources);
        for (const QString &problem : problems)
            std::fprintf(stderr, "%s\n", qPrintable(problem));
        if (!problems.isEmpty())
            return fail(QStringLiteral("%1 translatable string(s) or call(s) without a registered semantic context "
                                       "(i18n/contexts.tsv)").arg(problems.size()));
        std::printf("%d messages in %lld contexts, %lld sources: every context is registered\n", messages,
                    static_cast<long long>(extracted->contexts.size()), static_cast<long long>(sources.size()));
        return 0;
    }

    if (command == u"contextmap") {
        auto before = readKeys(option(QStringLiteral("before")));
        if (!before)
            return fail(before.error());
        auto after = readKeys(option(QStringLiteral("after")));
        if (!after)
            return fail(after.error());
        QList<TsCatalog> catalogs;
        for (const QString &path : options.values(QStringLiteral("catalog"))) {
            auto catalog = readKeys(path);
            if (!catalog)
                return fail(catalog.error());
            catalogs.append(*catalog);
        }
        auto map = buildContextMap(*before, *after, catalogs);
        if (!map)
            return fail(map.error());
        if (auto written = writeFile(option(QStringLiteral("out")), writeContextMap(*map)); !written)
            return fail(written.error());
        return 0;
    }

    if (command == u"recontext" || command == u"compare") {
        auto mapBytes = readFile(option(QStringLiteral("map")));
        if (!mapBytes)
            return fail(mapBytes.error());
        auto map = readContextMap(*mapBytes);
        if (!map)
            return fail(QStringLiteral("context map: %1").arg(map.error()));
        if (command == u"recontext") {
            // Every catalog moves, or none is written.
            QList<std::pair<QString, QByteArray>> outputs;
            for (const QString &path : options.values(QStringLiteral("ts"))) {
                auto catalog = readKeys(path);
                if (!catalog)
                    return fail(catalog.error());
                auto moved = recontext(*catalog, *map);
                if (!moved)
                    return fail(QStringLiteral("%1: %2").arg(path, moved.error()));
                outputs.append({path, writeTs(*moved)});
            }
            for (const auto &[path, bytes] : outputs)
                if (auto written = writeFile(path, bytes); !written)
                    return fail(written.error());
            return 0;
        }
        auto before = readKeys(option(QStringLiteral("before")));
        if (!before)
            return fail(before.error());
        auto after = readKeys(option(QStringLiteral("after")));
        if (!after)
            return fail(after.error());
        const RecontextComparison c = compareRecontexted(*before, *after, *map);
        for (const QString &problem : c.problems)
            std::fprintf(stderr, "%s\n", qPrintable(problem));
        for (const QString &entry : c.promoted)
            std::printf("joined a finished entry with the same text: %s\n", qPrintable(entry));
        std::printf("%s: entries %d -> %d (%d keys joined), with a translation %d -> %d, finished %d -> %d "
                    "(%lld unfinished joined a finished one), distinct translated texts %d -> %d\n",
                    qPrintable(after->language), c.entriesBefore, c.entriesAfter, c.joinedKeys, c.translatedBefore,
                    c.translatedAfter, c.finishedBefore, c.finishedAfter, static_cast<long long>(c.promoted.size()),
                    c.distinctTranslatedBefore, c.distinctTranslatedAfter);
        if (!c.problems.isEmpty())
            return fail(QStringLiteral("%1 difference(s)").arg(c.problems.size()));
        return 0;
    }
    return fail(QStringLiteral("unknown command %1").arg(command));
}
