#pragma once

#include <string>

namespace hikari::application {

enum class ClashAction { Replace, Rename, Skip };
struct ClashChoice {
    ClashAction action = ClashAction::Skip;
    std::u8string newName;
};

}
