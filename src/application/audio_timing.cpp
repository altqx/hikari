#include "hikari/application/audio_timing.h"

#include "hikari/core/checked.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

namespace hikari::application {

int legacyKeyFromPositionOrNone(std::span<const AudioLineSpan> lines, int position, int delta)
{
    const int count = static_cast<int>(lines.size());
    if (position > count)
        return -1;
    int visibleLines = 0;
    if (delta > 0) {
        for (int i = position + 1; i < count; i++) {
            if (lines[static_cast<std::size_t>(i)].visible)
                visibleLines++;
            if (delta == visibleLines)
                return i;
        }
        return -1;
    }
    if (delta < 0 && position > 0) {
        for (int i = position - 1; i >= 0; i--) {
            if (lines[static_cast<std::size_t>(i)].visible)
                visibleLines--;
            if (delta == visibleLines)
                return i;
        }
        return -1;
    }
    return position;
}

int legacyBoundarySnap(const AudioSnapContext &context, int ms, int rangeX, bool shiftHeld, bool keysnap,
                       bool otherLines)
{
    if (rangeX <= 0 || !context.view || context.view->sampleRate() <= 0)
        return ms;
    const AudioView &view = *context.view;
    const int w = view.width();
    // the range in milliseconds
    const int rangeMS = rangeX * view.samples() * 1000 / view.sampleRate();
    std::vector<int> boundaries;

    bool snapKey = context.snapToKeyframes;
    if (shiftHeld)
        snapKey = !snapKey;
    if (snapKey && context.drawKeyframes) {
        for (std::size_t i = 0; i < context.keyframesMs.size(); i++) {
            const int keyX = static_cast<int>(view.xAtMs(context.keyframesMs[i]));
            if (keyX >= 0 && keyX < w) {
                const int frameTime = i < context.keyframeSnapMs.size() ? context.keyframeSnapMs[i]
                                                                        : context.keyframesMs[i] - 21;
                boundaries.push_back(legacyZeroIt(frameTime));
            }
        }
    }

    // Other Lines' boundaries
    bool snapLines = context.snapToOtherLines;
    if (shiftHeld)
        snapLines = !snapLines;
    if (!otherLines)
        snapLines = false;
    const int count = static_cast<int>(context.lines.size());
    if (snapLines && (context.inactiveLines == 1 || context.inactiveLines == 2) && context.active >= 0 &&
        context.active < count) {
        int shadeFrom = 0, shadeTo = count - 1;
        if (context.inactiveLines == 1) {
            shadeFrom = legacyKeyFromPosition(context.lines, context.active, -1);
            shadeTo = legacyKeyFromPosition(context.lines, context.active, 1);
        }
        // A3-snap-bounds (R3, proposed): legacy read the Line before the first
        // (GetDialogue(-1)) when no Line is shown; the walk stays in the file.
        shadeFrom = std::max(shadeFrom, 0);
        shadeTo = std::min(shadeTo, count - 1);
        for (int j = shadeFrom; j <= shadeTo; j++) {
            if (j == context.active)
                continue;
            const auto &shade = context.lines[static_cast<std::size_t>(j)];
            if (!shade.visible)
                continue;
            const int shadeX1 = static_cast<int>(view.xAtMs(shade.startMs));
            const int shadeX2 = static_cast<int>(view.xAtMs(shade.endMs));
            if (shadeX1 >= 0 && shadeX1 < w)
                boundaries.push_back(shade.startMs);
            if (shadeX2 >= 0 && shadeX2 < w)
                boundaries.push_back(shade.endMs);
        }
    }

    // the nearest within range
    int minDist = rangeMS + 1;
    int bestMS = ms;
    for (const int boundary : boundaries) {
        const int adist = std::abs(ms - boundary);
        if (adist < minDist) {
            if (keysnap && adist < 10)
                continue;
            bestMS = boundary;
            minDist = adist;
        }
    }
    return legacyZeroIt(bestMS);
}

std::pair<int, int> legacyAddLead(int startMs, int endMs, bool in, bool out, int leadIn, int leadOut)
{
    if (in) {
        startMs -= leadIn;
        if (startMs < 0)
            startMs = 0;
    }
    if (out)
        endMs += leadOut;
    return {startMs, endMs};
}

void AudioTiming::drawn(const AudioView &view, const Selection &selection)
{
    // DoUpdateImage: selStart and selEnd are the Line's columns again (GetDialoguePos)
    m_selStart = static_cast<std::int64_t>(view.xAtMs(selection.startMs));
    m_selEnd = static_cast<std::int64_t>(view.xAtMs(selection.endMs));
    if (m_hasMark)
        m_selMark = static_cast<std::int64_t>(view.xAtMs(m_markMs));
}

bool AudioTiming::setMark(int ms)
{
    m_markMs = ms;
    return !std::exchange(m_hasMark, true);
}

void AudioTiming::lostCapture()
{
    m_hold = 0;
    m_holding = false;
}

AudioMouseResult AudioTiming::mouse(const AudioMouse &event, AudioView &view, Selection &selection,
                                    const AudioSnapContext &snap, const AudioTimingOptions &options,
                                    int scrollbarThickness)
{
    using Type = AudioMouse::Type;
    using Button = AudioMouse::Button;
    AudioMouseResult result;
    const std::int64_t x = event.x, y = event.y;
    const int w = view.width(), h = view.height();

    // Is inside? (AUDIO_AUTO_FOCUS and the cursor are the display's)
    bool onScale = false;
    if (x >= 0 && y >= 0 && x < w) {
        if (y < h)
            m_inside = true;
        else if (y < h + view.timelineHeight())
            onScale = true;
        if (m_inside && onScale) {
            result.redraw = true;
            m_inside = false;
        }
    } else {
        m_inside = false;
    }

    // wx's LeftDown/LeftDClick, RightDown/RightDClick, MiddleDown, LeftUp/RightUp
    const bool press = event.type == Type::Press;
    const bool leftDown = press && event.button == Button::Left;
    const bool rightDown = press && event.button == Button::Right;
    const bool middleDown = press && event.button == Button::Middle;
    const bool buttonDown = leftDown || rightDown;
    const bool buttonUP =
        event.type == Type::Release && (event.button == Button::Left || event.button == Button::Right);
    const bool controlOnly = event.ctrl && !event.alt && !event.shift; // GetModifiers() == wxMOD_CONTROL
    if (buttonDown || middleDown)
        result.focus = true;

    if (buttonUP && m_holding)
        m_holding = false;
    // Ctrl+left or middle click seeks the video
    if (((leftDown && controlOnly) || middleDown) && !onScale) {
        result.seekVideoMs = view.msAtX(x);
        result.redraw = true;
    }
    if (buttonDown && !m_holding)
        m_holding = true;

    // Scale dragging
    if ((m_hold == 0 && onScale) || m_draggingScale) {
        result.sizeCursor = false;
        if (rightDown) {
            result.markAdded = setMark(view.msAtX(x));
            result.redraw = true;
            return result;
        }
        if (leftDown) {
            m_lastDragX = x;
            m_draggingScale = true;
        } else if (m_holding) {
            const std::int64_t delta = m_lastDragX - x;
            m_lastDragX = x;
            view.updatePosition(view.position() + delta, false, scrollbarThickness);
            result.redraw = true;
            return result;
        } else {
            m_draggingScale = false;
        }
    }

    // Outside
    if (!m_inside && m_hold == 0)
        return result;

    // Timing (hasSel: the selection is drawn)
    if (!(event.ctrl && !event.alt && m_hold == 0)) {
        bool updated = false;
        if (m_hold == 0) {
            if (m_hasMark && std::abs(x - m_selMark) < 6) {
                result.sizeCursor = true;
                m_defCursor = false;
                if (buttonDown)
                    m_hold = 4;
            } else if (std::abs(x - m_selStart) < 6) {
                result.sizeCursor = true;
                m_defCursor = false;
                if (buttonDown)
                    m_hold = 1;
            } else if (std::abs(x - m_selEnd) < 6) {
                result.sizeCursor = true;
                m_defCursor = false;
                if (buttonDown)
                    m_hold = 2;
            } else if (buttonDown) {
                // Dragging nothing, time from scratch
                m_hold = leftDown ? 3 : 2;
                m_lastX = x;
            } else if (!m_defCursor) {
                result.sizeCursor = false;
                m_defCursor = true;
            }
        }

        if (m_hold != 0) {
            if (buttonUP) {
                // Prevent negative times
                selection.startMs = legacyZeroIt(std::max(0, selection.startMs));
                selection.endMs = legacyZeroIt(std::max(0, selection.endMs));
                m_selStart = std::max<std::int64_t>(0, m_selStart);
                m_selEnd = std::max<std::int64_t>(0, m_selEnd);
                // Alt: the next Line starts at the end, the previous one ends at the start
                if (m_hold == 2 && event.alt)
                    result.adjacent = AudioAdjacent::Next;
                else if ((m_hold == 1 || m_hold == 3) && event.alt)
                    result.adjacent = AudioAdjacent::Previous;
                if (m_hold != 4)
                    result.commit = m_hold == 2;
                m_hold = 0;
                return result;
            }
            // the mark
            if (m_hold == 4) {
                m_markMs = view.msAtX(x);
                updated = true;
            }
            // from nothing, or straight timing
            if (m_hold == 3) {
                if (leftDown)
                    selection.startMs = legacyBoundarySnap(snap, view.msAtX(x), 16, event.shift, false, !event.alt);
                else if (rightDown)
                    selection.endMs = view.msAtX(x);
                updated = true;
                selection.modified = true;
                if (event.leftHeld && std::abs(x - m_lastX) > options.startDragSensitivity) {
                    m_selStart = m_lastX;
                    m_selEnd = x;
                    selection.startMs =
                        legacyBoundarySnap(snap, view.msAtX(m_lastX), 16, event.shift, false, !event.alt);
                    selection.endMs = view.msAtX(x);
                    m_hold = 2;
                }
            }
            // the start
            if (m_hold == 1 && x != m_selStart) {
                const int snapped = legacyBoundarySnap(snap, view.msAtX(x), 16, event.shift, false, !event.alt);
                m_selStart = static_cast<std::int64_t>(view.xAtMs(snapped));
                selection.startMs = snapped;
                updated = true;
                selection.modified = true;
            }
            // the end
            if (m_hold == 2 && x != m_selEnd) {
                const int snapped = legacyBoundarySnap(snap, view.msAtX(x), 16, event.shift, false, !event.alt);
                m_selEnd = static_cast<std::int64_t>(view.xAtMs(snapped));
                selection.endMs = snapped;
                updated = true;
                selection.modified = true;
            }
        }
        if (updated) {
            result.playEnd = view.sampleAtX(static_cast<int>(m_selEnd));
            result.redraw = true;
            return result;
        }
    } else {
        m_hold = 0;
    }

    // Middle double click plays the selection
    if (event.type == Type::DoubleClick && event.button == Button::Middle) {
        result.focus = true;
        result.playSelection = true;
    }
    return result;
}

int legacyFieldTime(core::SubtitleFormat format, int ms)
{
    if (ms < 0)
        ms = 0; // SubsTime::NewTime
    switch (format) {
    case core::SubtitleFormat::Ass:
        return (ms / 10) * 10; // "H:MM:SS.cc"
    case core::SubtitleFormat::TMPlayer:
        return (ms / 1000) * 1000; // "HH:MM:SS"
    default:
        return ms;
    }
}

namespace {

constexpr std::int64_t kMarginMax = 9999;

core::DocumentTime msTime(std::int64_t ms)
{
    return core::DocumentTime(ms * 1000);
}

std::int64_t timeMs(const core::TimeField &field)
{
    return field.value.microseconds() / 1000;
}

} // namespace

std::expected<AudioCommitOutcome, CommandRefusal> commitAudioTimes(EditSession &session,
                                                                   const AudioCommitRequest &request,
                                                                   const LineVisible &shown)
{
    AudioCommitOutcome outcome;
    const auto active = session.selection().active;
    if (!active)
        return outcome;
    const auto lines = session.document().lines();
    const auto at = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == *active; });
    if (at == lines.end())
        return outcome;
    const int activeKey = static_cast<int>(at - lines.begin());
    std::vector<AudioLineSpan> spans;
    spans.reserve(lines.size());
    for (const auto *line : lines)
        spans.push_back({static_cast<int>(timeMs(line->start)), static_cast<int>(timeMs(line->end)),
                         !shown || shown(line->id)});

    // StartEdit/EndEdit->SetTime(time, true): a field already showing the time stays unmodified
    const auto format = session.document().format();
    const core::DocumentTime start = msTime(legacyFieldTime(format, request.startMs));
    const core::DocumentTime end = msTime(legacyFieldTime(format, request.endMs));
    const core::LineRecord current = session.draftLine() == active ? *session.draftRecord() : **at;
    DraftChange fields;
    if (current.start.value != start)
        fields.start = start;
    if (current.end.value != end)
        fields.end = end;
    if ((fields.start || fields.end) && !session.editDraft(*active, fields))
        return std::unexpected(session.isReadOnly() ? CommandRefusal::ReadOnly : CommandRefusal::Protected);

    // The Line the Alt release moves (CopyDialogueWithOffset with the grid's active Line)
    std::optional<core::LineId> adjacent;
    if (request.adjacent != AudioAdjacent::None) {
        const int key =
            legacyKeyFromPositionOrNone(spans, activeKey, request.adjacent == AudioAdjacent::Next ? 1 : -1);
        if (key >= 0)
            adjacent = lines[static_cast<std::size_t>(key)]->id;
    }
    auto moveAdjacent = [&](core::Document &d) {
        if (!adjacent)
            return;
        d.editLine(*adjacent, [&](core::LineRecord &l) {
            if (request.adjacent == AudioAdjacent::Next) {
                l.start.value = msTime(std::max(0, request.endMs));
                if (l.end.value < l.start.value)
                    l.end.value = msTime(std::max(0, request.endMs + 5000));
            } else {
                l.end.value = msTime(std::max(0, request.startMs));
                if (l.end.value < l.start.value)
                    l.start.value = msTime(std::max(0, request.startMs - 5000));
            }
        });
    };

    if (!request.save) {
        // A3-adjacent-step (proposed): legacy moved the other Line without a
        // step of its own (it joined the next one); here it is its own step.
        if (adjacent) {
            const auto ran = session.run(Command{"Changing time on audio spectrum", session.revision(), {*adjacent},
                                                 [&](core::Document &d) {
                                                     moveAdjacent(d);
                                                     return true;
                                                 }});
            if (!ran)
                return std::unexpected(ran.error());
            outcome.stepped = true;
        }
        return outcome;
    }

    // EditBox::Send: the draft's modified fields (E63-invalid-commit blocks an invalid draft)
    const auto draft = session.draftChange();
    std::optional<core::LineRecord> record;
    if (draft && draft->first == *active) {
        if (session.draftProblem() && session.invalidCommitPolicy() == InvalidCommitPolicy::Block)
            return std::unexpected(CommandRefusal::InvalidDraft);
        record = session.draftRecord();
    }
    const core::LineRecord &line = **at;
    const bool textChanged = record && record->text != line.text;
    const bool translationChanged = record && record->translation != line.translation;
    const bool startChanged = record && record->start.value != line.start.value;
    const bool endChanged = record && record->end.value != line.end.value;
    const bool marginLChanged = record && record->marginLeft.value != line.marginLeft.value;
    const bool marginRChanged = record && record->marginRight.value != line.marginRight.value;
    const bool marginVChanged = record && record->marginVertical.value != line.marginVertical.value;
    const bool cells = textChanged || translationChanged || startChanged || endChanged || marginLChanged ||
                       marginRChanged || marginVChanged;

    // SubsGrid::ChangeLine: one Line, or every selected Line with several selected
    std::vector<core::LineId> targets;
    const auto &selected = session.selection().selected;
    if (selected.size() < 2) {
        targets.push_back(*active);
    } else {
        for (const auto *l : lines)
            if (selected.contains(l->id))
                targets.push_back(l->id);
    }

    // SubsGrid::NextLine (nothing while the display holds a boundary)
    bool append = false;
    std::optional<core::LineId> next;
    if (request.nextLine && !request.holding) {
        const int key = legacyKeyFromPositionOrNone(spans, activeKey, 1);
        if (key >= 0)
            next = lines[static_cast<std::size_t>(key)]->id;
        else
            append = true;
    }

    if (!cells && !adjacent && !append) {
        outcome.next = next;
        return outcome;
    }

    std::set<core::LineId> touches(targets.begin(), targets.end());
    if (adjacent)
        touches.insert(*adjacent);
    if (append)
        touches.insert(lines.back()->id);
    const auto saved = draft;
    if (draft && draft->first == *active)
        session.discardDraft();
    std::optional<core::LineId> added;
    const auto ran = session.run(Command{
        append ? "Adding a new line" : "Changing time on audio spectrum", session.revision(), touches,
        [&](core::Document &d) {
            moveAdjacent(d);
            for (const auto &target : targets)
                d.editLine(target, [&](core::LineRecord &l) {
                    if (textChanged)
                        l.text = record->text;
                    if (translationChanged)
                        l.translation = record->translation;
                    if (startChanged)
                        l.start.value = record->start.value;
                    if (endChanged)
                        l.end.value = record->end.value;
                    if (marginLChanged)
                        l.marginLeft.value = std::clamp<std::int64_t>(record->marginLeft.value, 0, kMarginMax);
                    if (marginRChanged)
                        l.marginRight.value = std::clamp<std::int64_t>(record->marginRight.value, 0, kMarginMax);
                    if (marginVChanged)
                        l.marginVertical.value =
                            std::clamp<std::int64_t>(record->marginVertical.value, 0, kMarginMax);
                });
            if (append) {
                // NextLine on the last shown Line: a copy of the Line at the
                // key the last Line's row number gives (GetElementByKey(size
                // - 1): the shown Lines before the last one), starting at its
                // end, five seconds long, without text, added at the end.
                const auto now = d.lines();
                int shownBefore = 0;
                for (std::size_t i = 0; i + 1 < spans.size(); i++)
                    if (spans[i].visible)
                        shownBefore++;
                core::LineRecord copy = *now[static_cast<std::size_t>(shownBefore)];
                const auto eend = copy.end.value;
                copy.start.value = eend;
                copy.end.value = core::DocumentTime(eend.microseconds() + 5'000'000);
                copy.text.clear();
                copy.translation.clear();
                if (d.format() == core::SubtitleFormat::MicroDvd) {
                    // frames as the editor's NextLine gives them (C01-fps-isolation)
                    copy.startFrame = copy.endFrame;
                    copy.endFrame.reset();
                    if (const auto &rate = d.frameRate()) {
                        const auto &fps = rate->framesPerSecond();
                        if (const auto frame = core::mulDiv(copy.end.value.microseconds(), fps.numerator(),
                                                            fps.denominator(), 1'000'000, core::Rounding::Ceil))
                            copy.endFrame = *frame;
                    }
                }
                added = d.insertLineAfter(now.back()->id, copy);
                return added.has_value();
            }
            return true;
        }});
    if (!ran) {
        if (saved && saved->first == *active)
            session.editDraft(saved->first, saved->second); // the draft stays
        return std::unexpected(ran.error());
    }
    outcome.stepped = true;
    outcome.next = append ? added : next;
    return outcome;
}

} // namespace hikari::application
