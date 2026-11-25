#include "hikari/app/plugin_themes.h"

#include "hikari/api/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <set>

namespace hikari::app {

QString pluginThemeId(const std::string &plugin, const std::string &theme)
{
    return QStringLiteral("plugin:%1/%2").arg(QString::fromStdString(plugin), QString::fromStdString(theme));
}

std::vector<PluginTheme> pluginThemesOf(const std::string &id, const plugins::Manifest &manifest,
                                        const std::filesystem::path &root, bool shown)
{
    std::vector<PluginTheme> out;
    for (const plugins::ColourTheme &theme : manifest.themes)
        out.push_back({pluginThemeId(id, theme.id), QString::fromStdString(theme.title), QString::fromStdString(manifest.name),
                       themes::typeFromName(theme.type).value_or(themes::Type::Dark),
                       (root / std::filesystem::path(std::u8string(theme.path.begin(), theme.path.end()))).make_preferred(),
                       shown});
    return out;
}

namespace {

QByteArray readFile(const std::filesystem::path &path)
{
    QFile file(QString::fromStdU16String(path.u16string()));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

std::filesystem::path under(const std::filesystem::path &root, const std::string &path)
{
    return (root / std::filesystem::path(std::u8string(path.begin(), path.end()))).make_preferred();
}

}

void addPluginIcons(PluginIcons &into, const std::string &id, const plugins::Manifest &manifest,
                    const std::filesystem::path &root)
{
    for (const plugins::IconTheme &theme : manifest.iconThemes) {
        ui::icons::PluginIconTheme loaded{pluginThemeId(id, theme.id), QString::fromStdString(theme.title),
                                          QString::fromStdString(manifest.name), {}};
        for (const QString &role : ui::icons::roles())
            if (QByteArray svg = readFile(under(root, theme.folder + "/" + role.toStdString() + ".svg")); !svg.isEmpty())
                loaded.icons.insert(role, std::move(svg));
        into.themes.push_back(std::move(loaded));
    }
    for (const plugins::OwnIcon &icon : manifest.icons)
        if (QByteArray svg = readFile(under(root, icon.path)); !svg.isEmpty())
            into.own.insert(ui::icons::pluginIconRole(QString::fromStdString(id), QStringLiteral("own:") + QString::fromStdString(icon.id)),
                            std::move(svg));
}

void syncPluginIcons(PluginIcons icons)
{
    std::vector<QString> going;
    for (const ui::icons::PluginIconTheme &installed : ui::icons::themes())
        if (std::ranges::find(icons.themes, installed.id, &ui::icons::PluginIconTheme::id) == icons.themes.end())
            going.push_back(installed.id);
    for (const QString &id : going)
        ui::icons::uninstallTheme(id);
    for (ui::icons::PluginIconTheme &theme : icons.themes)
        ui::icons::installTheme(std::move(theme));
    ui::icons::setOwnIcons(std::move(icons.own));
}

QStringList syncPluginThemes(const std::vector<PluginTheme> &themes)
{
    QStringList problems;
    std::set<QString> shown;
    for (const PluginTheme &theme : themes) {
        if (!theme.shown) {
            if (!ui::theme::find(theme.id))
                ui::theme::fallBackFrom(theme.id, theme.type);
            continue;
        }
        QFile file(QString::fromStdU16String(theme.file.u16string()));
        const QByteArray json = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        const auto loaded = themes::load(std::string_view(json.constData(), std::size_t(json.size())));
        if (!loaded) {
            problems << QStringLiteral("%1: %2 %3").arg(theme.title, QString::fromStdString(loaded.error().path),
                                                       QString::fromStdString(loaded.error().message));
            continue;
        }
        shown.insert(theme.id);
        ui::theme::install({theme.id, theme.title, theme.plugin, *loaded});
    }
    std::vector<QString> going;
    for (const ui::theme::Entry &entry : ui::theme::entries())
        if (!entry.plugin.isEmpty() && !shown.contains(entry.id))
            going.push_back(entry.id);
    for (const QString &id : going)
        ui::theme::uninstall(id);
    return problems;
}

namespace {

std::string slugOf(const QString &themeId)
{
    std::string slug;
    for (const QChar c : themeId.mid(themeId.lastIndexOf(QLatin1Char('/')) + 1)) {
        if (c.isUpper() && !slug.empty() && slug.back() != '-')
            slug += '-';
        const char ascii = c.toLower().toLatin1();
        if ((ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9'))
            slug += ascii;
        else if (!slug.empty() && slug.back() != '-')
            slug += '-';
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    return slug.empty() ? std::string("theme") : slug;
}

QString author()
{
    for (const char *name : {"USER", "USERNAME"})
        if (const QString user = qEnvironmentVariable(name); !user.isEmpty())
            return user;
    return QDir::home().dirName();
}

bool writeFile(const std::filesystem::path &path, const std::string &text)
{
    QFile file(QString::fromStdU16String(path.u16string()));
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        && file.write(text.data(), qint64(text.size())) == qint64(text.size()) && file.flush();
}

}

std::expected<std::filesystem::path, QString> writeThemePlugin(const ui::theme::Entry &theme,
                                                               const std::filesystem::path &parent,
                                                               const std::function<bool(const std::string &id)> &taken)
{
    const std::string slug = slugOf(theme.id);
    const std::string title = QCoreApplication::translate("HikariSub.Plugins", "%1 copy", "a theme plugin saved from a theme")
                                  .arg(ui::theme::shownTitle(theme))
                                  .toStdString();
    std::string id, name;
    std::filesystem::path folder;
    for (int n = 1;; ++n) {
        name = slug + "-copy" + (n > 1 ? "-" + std::to_string(n) : std::string());
        id = "local." + name;
        folder = parent / name;
        std::error_code ignored;
        if (!taken(id) && !std::filesystem::exists(folder, ignored))
            break;
    }

    const std::string themeFile = "themes/" + slug + ".json";
    api::Json manifest = api::Json::object(
        {{"id", id},
         {"name", title},
         {"version", "1.0.0"},
         {"api", 1},
         {"authors", api::Json::array({author().toStdString()})},
         {"contributes",
          api::Json::object({{"themes", api::Json::array({api::Json::object({{"id", slug},
                                                                             {"title", title},
                                                                             {"type", themes::typeName(theme.file.type)},
                                                                             {"path", themeFile}})})}})}});
    const auto failed = [&folder] {
        return std::unexpected(QCoreApplication::translate("HikariSub.Plugins", "Can’t write the theme plugin to %1.")
                                   .arg(QDir::toNativeSeparators(QString::fromStdU16String(folder.u16string()))));
    };
    std::error_code error;
    if (!std::filesystem::create_directories(folder / "themes", error))
        return failed();
    if (!writeFile(folder / "hikari-plugin.json", manifest.dump(2) + "\n")
        || !writeFile(folder / std::filesystem::path(themeFile).make_preferred(), themes::write(themes::complete(theme.file))))
        return failed();
    return folder;
}

}
