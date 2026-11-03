#pragma once

#include "hikari/core/document.h"
#include "hikari/core/extradata.h"

#include <vector>

namespace hikari::test {

inline core::Document withoutKaraokeMarks(core::Document document)
{
    std::vector<core::LineId> ids;
    for (const auto *line : document.lines())
        if (core::extradataValue(line->extra, core::kKaraSourceKey) || core::extradataValue(line->extra, core::kKaraFromKey))
            ids.push_back(line->id);
    for (const auto id : ids)
        document.editLine(id, [](core::LineRecord &line) {
            line.extra = core::withoutExtradata(core::withoutExtradata(line.extra, core::kKaraSourceKey), core::kKaraFromKey);
        });
    return document;
}

}
