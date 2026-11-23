#pragma once

#include <string_view>

namespace hikari::application {

// The export format id for one that 0.1.0-beta.4 wrote in export.choices or a Project's [export]; others are unchanged.
constexpr std::string_view currentExportFormat(std::string_view id)
{
    if (id == "txt")
        return "text";
    if (id == "mdvd")
        return "microdvd";
    if (id == "tmp")
        return "tmplayer";
    return id;
}

}
