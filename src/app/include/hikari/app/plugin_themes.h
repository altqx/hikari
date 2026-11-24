#pragma once

#include "hikari/plugins/manifest.h"
#include "icon_theme.h"
#include "hikari/themes/theme.h"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>
#include <vector>

namespace hikari::app {

struct PluginTheme {
    QString id; // plugin:<plugin id>/<theme id>
    QString title;
    QString plugin; // the plugin's name
    themes::Type type = themes::Type::Dark;
    std::filesystem::path file;
    bool shown = false;
};

QString pluginThemeId(const std::string &plugin, const std::string &theme);

// The manifest's colour themes, under the package at `root`.
std::vector<PluginTheme> pluginThemesOf(const std::string &id, const plugins::Manifest &manifest,
                                        const std::filesystem::path &root, bool shown);

// Makes the shown themes choosable and every other plugin theme go (docs/plugins.md, Themes and icons).
// Returns why a shown theme couldn't be loaded, one line each.
QStringList syncPluginThemes(const std::vector<PluginTheme> &themes);

struct PluginIcons {
    std::vector<ui::icons::PluginIconTheme> themes;
    QHash<QString, QByteArray> own; // by ui::icons::pluginIconRole
};

// A shown plugin's icon themes, holding the roles they cover, and its own icons.
void addPluginIcons(PluginIcons &into, const std::string &id, const plugins::Manifest &manifest,
                    const std::filesystem::path &root);
// Makes exactly these icon themes choosable and these own icons drawable.
void syncPluginIcons(PluginIcons icons);

}
