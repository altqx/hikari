#pragma once

#include "hikari/application/node_catalogue.h"

#include <QString>

#include <string>
#include <string_view>
#include <vector>

namespace hikari::ui {

// The node catalogue's names and summaries in the user's language ("HikariSub.Nodes").
QString nodeTitle(std::string_view type);
QString nodeSummary(std::string_view type);
QString nodeFieldSummary(std::string_view type, std::string_view field);
QString nodeCategoryTitle(std::string_view category);

// Catalogue nodes, categories and fields ("type.field") whose text has no translation entry, or
// one that no longer matches the catalogue.
std::vector<std::string> untranslatedCatalogueText(const std::vector<application::NodeSpec> &catalogue);

}
