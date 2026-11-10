#pragma once

#include "hikari/application/gradient.h"
#include "hikari/application/visual_tools.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application::visual {

// The tags the Values stops offer, in the picker's order.
struct GradientTag {
    std::string_view key; // a lerp::TagSpec key
    std::u16string_view name;
    int section = 0;
};
const std::vector<GradientTag> &gradientTags();
std::u16string_view gradientProblemText(gradient::Problem problem);

// Previews the gradient after a drag (or at once where no drag is needed) until Apply or Enter commits it as
// one step or Esc drops it.
class GradientTool : public VisualTool {
public:
    Family family() const override { return Family::Gradient; }
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

    const gradient::Spec &spec() const { return m_spec; }
    // The current tag's key ("" without one).
    const std::string &currentTag() const { return m_current; }
    // Why the placed gradient makes no Lines.
    const std::optional<gradient::Problem> &problem() const { return m_problem; }
    bool apply(VisualHost &host);

private:
    struct Target {
        core::LineRecord record;
        gradient::LineIn in;
    };
    std::vector<Target> targets(const VisualHost &host) const;
    bool needsDrag() const;
    void place(VisualHost &host);
    void seed(VisualHost &host);
    void refresh(const VisualHost &host);
    std::optional<gradient::Point> handle(int index) const;
    void moveHandle(int index, gradient::Point to);
    int handleAt(PointF at, const VisualHost &host) const;
    gradient::TagStops *stopsOf(const std::string &key);
    void addTag(std::string_view key, const VisualHost &host);

    gradient::Spec m_spec;
    std::string m_current;
    bool m_placed = false;
    int m_grabbed = -1;
    bool m_fresh = false;
    bool m_lineSet = false, m_maskSet = false;
    std::optional<gradient::Problem> m_problem;
    std::vector<core::LineRecord> m_preview;
    std::vector<core::LineId> m_hidden;
    std::vector<std::pair<core::LineId, std::vector<std::u8string>>> m_replaced;
    std::vector<std::pair<core::LineId, std::u8string>> m_changed;
};

}
