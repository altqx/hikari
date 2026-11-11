#pragma once

#include "hikari/application/grid_commands.h"
#include "hikari/core/tag_cleanup.h"

#include <expected>
#include <string>
#include <vector>

namespace hikari::application {

// Selected: the shown selected Lines, Comments too. File: every Line but Comments, as Clean Tags
// runs.
enum class TagRemovalLines { Selected, File };

struct TagRemovalChange {
    core::LineId id;
    int row = 0; // 1-based, in the Document
    std::u8string before, after;
    bool deleted = false; // left empty with Cleanup::deleteEmptyLines
    bool operator==(const TagRemovalChange &) const = default;
};

std::vector<TagRemovalChange> tagRemovalChanges(const EditSession &session, const core::tagcleanup::Options &options,
                                                TagRemovalLines lines, const LineVisible &visible = {});

// One undo step, "Remove tags"; refused when nothing changes. Returns the Lines changed or deleted.
std::expected<int, CommandRefusal> removeTags(EditSession &session, const core::tagcleanup::Options &options,
                                              TagRemovalLines lines, const LineVisible &visible = {});

}
