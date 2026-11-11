#pragma once

#include <QVariantMap>

#include <string_view>

namespace hikari::ui {

/// SearchSuggestions.qml's properties for Google's snippet: {chips: [{text, url}], lightLogo, darkLogo}, the logos as
/// data: URLs. Empty when the snippet has no chip.
QVariantMap searchSuggestionsView(std::string_view html);

}
