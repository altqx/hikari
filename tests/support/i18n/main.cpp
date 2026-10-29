// hikari_i18n_lint: the O6 semantic contexts' lint (i18n/contexts.tsv,
// docs/localisation.md) that the ctest hikari_i18n_contexts_are_semantic runs.
//
//   hikari_i18n_lint --contexts <contexts.tsv> --extracted <located.ts> --sources <dir>

#include "hikari/i18n/contexts.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>

#include <algorithm>
#include <cstdio>

using namespace hikari::i18n;

namespace {

int fail(const QString &message)
{
    std::fprintf(stderr, "hikari_i18n_lint: %s\n", qPrintable(message));
    return 1;
}

std::expected<QByteArray, QString> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("cannot read %1: %2").arg(path, file.errorString()));
    return file.readAll();
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
    const QStringList args = app.arguments().mid(1);
    QMap<QString, QString> options;
    for (qsizetype i = 0; i < args.size(); i += 2) {
        if (!args[i].startsWith(QStringLiteral("--")) || i + 1 >= args.size())
            return fail(QStringLiteral("usage: hikari_i18n_lint --contexts <contexts.tsv> --extracted <located.ts> --sources <dir>"));
        if (options.contains(args[i].mid(2)))
            return fail(QStringLiteral("%1 given twice").arg(args[i]));
        options.insert(args[i].mid(2), args[i + 1]);
    }
    const auto option = [&](const QString &name) { return options.value(name); };
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
