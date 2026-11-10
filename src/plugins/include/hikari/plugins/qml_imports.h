#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::plugins {

// The only modules a QML panel may import; paths inside its own package are allowed too.
inline constexpr std::array<std::string_view, 4> kQmlPanelModules{"QtQuick", "QtQuick.Controls", "QtQuick.Layouts",
                                                                  "Hikari.Plugin"};

// .qml, .js and .mjs: the files the import rule reads.
bool isQmlSource(std::string_view path);

// The first import in `text`, the package file at `path`, that a QML panel may not make, as written: a module name,
// or a quoted path that is absolute, has a scheme or climbs out of the package. A supported-API rule, not a
// security boundary.
std::optional<std::string> qmlImportProblem(std::string_view path, std::string_view text);

}
