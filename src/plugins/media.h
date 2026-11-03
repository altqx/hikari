#pragma once

#include "hikari/plugins/package.h"

#include <optional>

namespace hikari::plugins {

std::optional<PackageProblem> mediaProblem(const Manifest &manifest, const std::vector<PackageFile> &files);

}
