#pragma once

#include "hikari/application/text_on_path.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"

#include <optional>
#include <string>
#include <vector>

namespace hikari::application::visual {

// Places the active Line's text along a path, one Line per grapheme cluster, seeded from its vector clip or drawn
// with the vector clip's point editor. The new Lines preview until Apply or Enter makes them one step.
class TextOnPathTool : public VisualTool {
public:
    enum class Problem { NoPath, NoText, NoMeasure, NoFrames };

    Family family() const override { return Family::TextOnPath; }
    void reset(VisualHost &host) override;
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool takesKey(const Key &event) const override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    bool setValue(const std::string &name, const std::u16string &text, VisualHost &host,
                  ValueEdit edit = ValueEdit::Commit) override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;
    void targetsChanged(VisualHost &host) override;
    bool cancelPending(VisualHost &host) override;
    bool hasPending() const override;
    std::vector<core::LineRecord> previewLines(const VisualHost &host) const override;
    std::vector<core::LineId> hiddenLines(const VisualHost &host) const override;

    bool apply(VisualHost &host);
    const std::optional<Problem> &problem() const { return m_problem; }
    double overflow() const { return m_overflow; }
    int lineCount() const;
    VectorEditor &editor() { return m_editor; }

private:
    VectorFrame frame(const VisualHost &host) const;
    VectorEditor::Callbacks callbacks(VisualHost &host);
    void refresh(VisualHost &host);

    VectorEditor m_editor;
    double m_divisor = 1;
    text_on_path::Options m_options;
    bool m_slide = false;
    int m_every = 1;
    std::optional<core::LineId> m_target;
    std::optional<Problem> m_problem;
    double m_overflow = 0;
    std::vector<core::LineRecord> m_preview;
    std::vector<Replacement> m_copies;
};

std::u16string_view textOnPathProblemText(TextOnPathTool::Problem problem);

}
