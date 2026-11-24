#include "hikari/app/plugin_themes.h"

#include "theme.h"

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

}
