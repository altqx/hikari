#pragma once

#include "hikari/application/write_coordinator.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hikari::application {

enum class MarkColour { Accent, Warning, Danger, Success, Muted };

struct RowMark {
    core::LineId line;
    MarkColour colour = MarkColour::Accent;
    std::string tooltip;
    bool operator==(const RowMark &) const = default;
};

// One owner's marks on one Line, as the Grid shows them.
struct OwnedMark {
    std::string owner;
    std::string label; // the owner's name, shown before the tooltip
    MarkColour colour = MarkColour::Accent;
    std::string tooltip;
    bool operator==(const OwnedMark &) const = default;
};

// Grid row marks that plugins set, by owner and Document; never saved.
class RowMarks {
public:
    static constexpr std::size_t kMostPerDocument = 20000;

    // Replaces the owner's marks on the Lines named; false when it would hold more than kMostPerDocument.
    bool set(const std::string &owner, const std::string &label, DocumentId document, const std::vector<RowMark> &marks);
    // Every Line when `lines` is empty.
    void clear(const std::string &owner, DocumentId document, const std::vector<core::LineId> &lines = {});
    void clearOwner(const std::string &owner);
    // Drops the marks of every Document not in `open`.
    void keepOnly(const std::vector<DocumentId> &open);
    std::vector<RowMark> of(const std::string &owner, DocumentId document) const;
    // Owner order, then line id.
    std::map<std::uint64_t, std::vector<OwnedMark>> shown(DocumentId document) const;
    // Per Document, how many Lines the owner marks.
    std::map<std::uint64_t, std::size_t> counts(const std::string &owner) const;

    // The Document whose marks changed.
    void setObserver(std::function<void(DocumentId)> changed) { m_changed = std::move(changed); }

private:
    struct Owned {
        std::string label;
        std::map<std::uint64_t, std::map<std::uint64_t, RowMark>> documents; // document, line
    };
    void changed(DocumentId document) const;

    std::map<std::string, Owned> m_owners;
    std::function<void(DocumentId)> m_changed;
};

}
