#pragma once

// Coordinates are in the video window's device pixels.

#include "hikari/application/visual_script.h"
#include "hikari/application/visual_tools.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace hikari::application::visual {

// In the alignment choice's order; the tool value takes the index + 1.
const std::array<std::u16string_view, 21> &positionAlignments();

class PositionTool : public VisualTool {
public:
    Family family() const override { return Family::Position; }
    void reset(VisualHost &host) override;
    void selected(VisualHost &host) override;
    void pointer(const Pointer &event, VisualHost &host) override;
    bool takesKey(const Key &event) const override;
    bool key(const Key &event, VisualHost &host) override;
    Overlay overlay(const VisualHost &host) const override;
    std::optional<LineWarning> warning(const VisualHost &host) const override;
    void blocked(VisualHost &host) override;
    std::vector<ToolValue> values(const VisualHost &host) const override;
    bool setValue(const std::string &name, const std::u16string &text, VisualHost &host,
                  ValueEdit edit = ValueEdit::Commit) override;
    std::vector<ToolOption> options(const VisualHost &host) const override;
    bool setOption(const std::string &name, int value, VisualHost &host) override;

    // move: the \move end point in the view, then absolute times.
    struct Data {
        core::LineId line;
        PointF pos, lastpos;
        std::size_t textStart = 0, textLength = 0;
        bool putInBracket = false;
        std::optional<std::array<double, 4>> move;
    };
    const std::vector<Data> &data() const { return m_data; }
    // The alignment + 1, plus 32 (by rectangle), 64 (X) and 128 (Y).
    int toolValue() const;
    bool rectangleVisible() const { return m_rectangleVisible; }
    const std::array<PointF, 2> &rectangle() const { return m_rectangle; }
    int alignment() const { return m_alignment; }
    int lineAlignment() const { return m_curLineAlignment; }
    void applyToolValue(int value, VisualHost &host);

private:
    void setCurVisual(VisualHost &host, bool fromGesture);
    void changeTool(int tool, bool blockSetCurVisual, VisualHost &host);
    bool stageAll(VisualHost &host);
    bool commitAll(VisualHost &host);
    bool ensureGesture(VisualHost &host);
    void setMovePosition(Data &data, int time) const;
    void savePosition(VisualHost &host);
    void setPointFor(core::LineId active, PointF point);
    int hitTest(PointF pos, bool diff, const VisualHost &host);
    void sortPoints();
    void setPosition(VisualHost &host);
    PointF positionToVideo(PointF point, bool changeX, bool changeY, const VisualHost &host) const;
    void getPositioningData(VisualHost &host);
    void rectanglePointer(const Pointer &event, VisualHost &host);
    bool cancelledByHost(VisualHost &host);

    std::vector<Data> m_data;
    // The helper cross a middle click places.
    int m_helperX = 0, m_helperY = 0;
    bool m_hasHelperLine = false;
    bool m_movingHelperLine = false;
    PointF m_firstmove;
    unsigned char m_axis = 0;
    // "Set position by rectangle".
    std::array<PointF, 2> m_rectangle{};
    PointF m_textSize;
    std::array<PointF, 2> m_border{};
    PointF m_extlead;
    PointF m_drawingPosition;
    PointF m_diffs;
    PointF m_curLinePosition;
    bool m_hasRectangle = false;
    bool m_hasX = false;
    bool m_hasY = false;
    bool m_rectangleVisible = false;
    int m_grabbed = -1;
    unsigned char m_alignment = 1;
    unsigned char m_curLineAlignment = static_cast<unsigned char>(-1);
    double m_moveValues[7] = {0, 0, 0, 0, 0, 0, 0};
    // Rectangle, X, Y; m_an is the alignment index.
    std::array<bool, 3> m_toggled{false, true, true};
    int m_an = 0;
    // m_cancelled: a drag Esc cancelled, ignored until the next press.
    bool m_inGesture = false;
    bool m_cancelled = false;
    bool m_keyNudge = false;
    bool m_dataFromStaged = false;
    bool m_valueEdit = false; // the open gesture is a scrubbed value's
    bool m_dummyCommit = false;
    bool m_skipReset = false;
    std::uint64_t m_committedRevision = 0;
};

class MoveTool : public VisualTool {
public:
    Family family() const override { return Family::Move; }
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

    PointF from() const { return m_from; }
    PointF to() const { return m_to; }
    int moveStart() const { return m_moveStart; }
    int moveEnd() const { return m_moveEnd; }
    bool twoPoints() const { return m_hasLineToMove; }
    PointF lineToMoveStart() const { return m_lineToMoveStart; }
    PointF lineToMoveEnd() const { return m_lineToMoveEnd; }

private:
    void setCurVisual(VisualHost &host);
    bool setMove(VisualHost &host);
    void commit(VisualHost &host);
    // Stages when dummy, else commits.
    void setVisual(bool dummy, VisualHost &host);
    std::u16string changeVisualBatch(const std::u16string &text, const core::LineRecord &line,
                                     const ScriptState &state, const core::LineRecord &editLine,
                                     const VisualHost &host) const;
    bool cancelledByHost(VisualHost &host);
    int activeStart(const VisualHost &host) const; // ms

    PointF m_from, m_to, m_lastFrom, m_lastTo, m_firstmove, m_lastmove, m_moveDistance;
    int m_moveStart = 0, m_moveEnd = 0;
    unsigned char m_type = 0;
    int m_grabbed = -1;
    int m_diffsX = 0, m_diffsY = 0;
    unsigned char m_axis = 0;
    double m_moveValues[7] = {0, 0, 0, 0, 0, 0, 0};
    int m_helperX = 0, m_helperY = 0;
    bool m_hasHelperLine = false;
    bool m_movingHelperLine = false;
    // "Set movement using 2 points and the video position".
    bool m_hasLineToMove = false;
    PointF m_lineToMoveStart{0, 0}, m_lineToMoveEnd{0, 0};
    bool m_lineToMoveVisibility[2] = {false, false};
    int m_lastVideoTime = -1;
    int m_lineStartTime = -1;
    bool m_skipReset = false;
    bool m_committing = false;
    std::uint64_t m_committedRevision = 0;
    std::optional<core::LineId> m_committedActive;
    bool m_inGesture = false;
    bool m_cancelled = false;
    bool m_keyNudge = false;
    bool m_valueEdit = false; // the open gesture is a scrubbed value's
    // Typed \move times, ms from each Line's start.
    std::array<std::optional<int>, 2> m_typedTimes;
};

}
