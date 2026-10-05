// hikari_i18n_migrate: the reproducible O4 conversion of the legacy PO
// catalogs into TS (docs/qt/localisation.md). Developer-invoked through the
// hikari_i18n_keymap and hikari_i18n_convert targets; ordinary builds only run
// its probe subcommand for the plural tests.
//
//   keymap  --pot <template.pot> --keys <keys.ts> --commit <sha> --out <keymap.tsv>
//   convert --map <keymap.tsv> --keys <keys.ts> --pot <template.pot>
//           --po <language>=<file.po>... --out-dir <dir> --report <report.md>
//   probe   --po <file.po|template.pot> --language <language> --out <file.ts>

#include "hikari/i18n/migration.h"

#include <QCoreApplication>
#include <QDir>
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
        return fail(QStringLiteral("usage: hikari_i18n_migrate keymap|convert|probe [options]"));
    const QString command = args.takeFirst();
    QMultiMap<QString, QString> options;
    for (qsizetype i = 0; i < args.size(); i += 2) {
        if (!args[i].startsWith(QStringLiteral("--")) || i + 1 >= args.size())
            return fail(QStringLiteral("expected --option value pairs, got %1").arg(args[i]));
        options.insert(args[i].mid(2), args[i + 1]);
    }
    const auto option = [&](const QString &name) { return options.value(name); };
    for (const QString &name : options.uniqueKeys())
        if (name != u"po" && options.count(name) > 1)
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
    return fail(QStringLiteral("unknown command %1").arg(command));
}
