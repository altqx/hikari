#pragma once

#include "hikari/application/qc_notes.h"
#include "hikari/application/write_coordinator.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Worst first.
enum class ProblemSeverity { Error, Warning, Info, Hint };
enum class ProblemField { Text, Translation };

// A replacement by UTF-16 offsets in the text the problem was set against.
struct ProblemEdit {
    std::size_t from = 0, to = 0;
    std::string insert;
    bool operator==(const ProblemEdit &) const = default;
};

struct ProblemFix {
    std::string title;
    std::vector<ProblemEdit> edits;
    bool operator==(const ProblemFix &) const = default;
};

struct Problem {
    core::LineId line;
    ProblemField field = ProblemField::Text;
    // UTF-16 offsets in the field; the whole Line without them.
    std::optional<std::size_t> from, to;
    ProblemSeverity severity = ProblemSeverity::Error;
    std::string message;
    std::string code;
    std::vector<ProblemFix> fixes;
    // The field's text when the problem was set; the problem goes once the field holds anything else.
    std::u8string against;
    bool operator==(const Problem &) const = default;
};

struct HeldProblem {
    std::string id;
    std::string source;
    std::string set;
    Problem problem;
};

const std::u8string &problemFieldText(const core::LineRecord &line, ProblemField field);

// The problems sources set on Documents; never saved. A source is a plugin or one of its language servers,
// held by an owner whose problems all go together.
class Problems {
public:
    static constexpr std::size_t kMostPerDocument = 20000;
    static constexpr std::string_view kQcSource = "qc";

    // Replaces the source's problems under `set`; false when the owner would hold more than kMostPerDocument there.
    bool set(const std::string &owner, const std::string &source, const std::string &name, DocumentId document,
             const std::string &set, std::vector<Problem> problems);
    // Every Document without `document`, every set without `set`; answers how many went.
    std::size_t clear(const std::string &source, std::optional<DocumentId> document, std::optional<std::string> set);
    void clearOwner(const std::string &owner);
    // Forgets every Document not in `open`.
    void keepOnly(const std::vector<DocumentId> &open);
    // Drops the problems whose Line went or whose field changed, and tells when the open QC notes changed.
    void refresh(DocumentId id, const core::Document &document);

    // Source order, then set, then as set.
    std::vector<HeldProblem> of(DocumentId document) const;
    const HeldProblem *find(DocumentId document, std::string_view id) const;
    // Whether `id` is one this store gave, held or not.
    bool gave(std::string_view id) const;
    // The name the source's problems are shown under; empty for a source that holds none.
    std::string name(const std::string &source) const;

    void setObserver(std::function<void(DocumentId, std::vector<std::string> sources)> changed)
    {
        m_changed = std::move(changed);
    }

private:
    struct Source {
        std::string owner;
        std::string name;
        std::map<std::uint64_t, std::map<std::string, std::vector<HeldProblem>>> documents; // document, set
    };
    std::size_t held(const std::string &owner, DocumentId document, const std::string &source, const std::string &set) const;
    void changed(DocumentId document, std::vector<std::string> sources) const;

    std::map<std::string, Source> m_sources;
    std::map<std::uint64_t, std::vector<QcNote>> m_openNotes;
    std::uint64_t m_next = 1;
    std::function<void(DocumentId, std::vector<std::string>)> m_changed;
};

}
