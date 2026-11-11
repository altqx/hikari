#include "hikari/app/application.h"
#include "hikari/application/tag_removal.h"

#include <QCoreApplication>
#include <QVariantList>

namespace hikari::app {

namespace {

namespace tc = core::tagcleanup;

constexpr qsizetype kPreviewRows = 200;

QString qstring(std::u8string_view text)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size()));
}

tc::Options tagRemovalOptions(const QVariantMap &map)
{
    tc::Options options;
    for (const auto &family : map.value(QStringLiteral("families")).toList()) {
        const int f = family.toInt();
        if (f >= 0 && static_cast<std::size_t>(f) < tc::kFamilyCount)
            options.families.set(static_cast<std::size_t>(f));
    }
    const QString scope = map.value(QStringLiteral("scope")).toString();
    options.scope = scope == QLatin1String("start")    ? tc::BlockScope::Start
                    : scope == QLatin1String("inline") ? tc::BlockScope::Inline
                                                       : tc::BlockScope::All;
    const auto flag = [&](const char *key) { return map.value(QString::fromLatin1(key)).toBool(); };
    options.cleanup = {flag("removeComments"), flag("mergeBlocks"), flag("dropRedundant"), flag("cleanTags"),
                       flag("deleteEmptyLines")};
    return options;
}

application::TagRemovalLines tagRemovalLines(const QVariantMap &map)
{
    return map.value(QStringLiteral("lines")).toString() == QLatin1String("file") ? application::TagRemovalLines::File
                                                                                   : application::TagRemovalLines::Selected;
}

}

QVariantMap Application::tagRemovalPreview(const QVariantMap &options) const
{
    const auto *session = targetSession();
    if (!session)
        return {};
    const auto changes =
        application::tagRemovalChanges(*session, tagRemovalOptions(options), tagRemovalLines(options), shownLines());
    QVariantList rows;
    int deleted = 0;
    for (const auto &c : changes) {
        deleted += c.deleted;
        if (rows.size() < kPreviewRows)
            rows.append(QVariantMap{{QStringLiteral("row"), c.row},
                                    {QStringLiteral("before"), qstring(c.before)},
                                    {QStringLiteral("after"), qstring(c.after)},
                                    {QStringLiteral("deleted"), c.deleted}});
    }
    return {{QStringLiteral("changed"), static_cast<int>(changes.size()) - deleted},
            {QStringLiteral("deleted"), deleted},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("more"), static_cast<int>(changes.size()) - static_cast<int>(rows.size())}};
}

QVariantMap Application::removeTags(const QVariantMap &options)
{
    int changed = 0;
    const bool done = runFilter([&](application::EditSession &s) -> std::expected<void, application::CommandRefusal> {
        const auto n = application::removeTags(s, tagRemovalOptions(options), tagRemovalLines(options), shownLines());
        if (!n)
            return std::unexpected(n.error());
        changed = *n;
        return {};
    });
    const QString text = done ? QCoreApplication::translate("HikariSub.Tags", "Changed %n line(s)", nullptr, changed)
                              : QCoreApplication::translate("HikariSub.Tags", "No line changes.");
    return {{QStringLiteral("ok"), done}, {QStringLiteral("text"), text}};
}

}
