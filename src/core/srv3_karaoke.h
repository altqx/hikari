#pragma once

#include "srv3_ass.h"

#include <memory>
#include <utility>
#include <vector>

namespace hikari::core::srv3 {

struct KaraokeStepContext {
    AssDocument *document = nullptr;
    LinePtr originalLine;
    std::vector<std::pair<Ticks, int>> activeSectionsPerStep;
    LinePtr stepLine;
    std::size_t stepIndex = 0;
    int numActiveSections = 0;
    std::size_t singingBegin = 0, singingEnd = 0;
};

class KaraokeType {
public:
    virtual ~KaraokeType() = default;
    // SimpleKaraokeType itself, not a subclass: the only type YouTube's native karaoke can show.
    virtual bool isPlainSimple() const { return false; }
    virtual std::vector<LinePtr> apply(KaraokeStepContext &context) const = 0;
};

std::shared_ptr<const KaraokeType> simpleKaraokeType();
std::shared_ptr<const KaraokeType> fadeKaraokeType();
std::shared_ptr<const KaraokeType> glitchKaraokeType();
std::shared_ptr<const KaraokeType> cursorKaraokeType(std::vector<u16> cursors, Ticks interval, bool beforeSinging);

std::vector<LinePtr> emulatedKaraokeLines(AssDocument &doc, const LinePtr &line);
std::vector<LinePtr> animatorExpand(const AssDocument &doc, const LinePtr &line);
std::vector<Section> cursorSections(AssDocument &doc, const Section &initialFormatting, u16v cursor);

}
