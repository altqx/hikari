#pragma once

// PU5 (#307): the history of the "Edit shapes" and "Edit tags" windows. A
// change is kept as it is made; each is a step that Undo and Redo walk
// while the window is open (the list and its selection before and after).
// Typing into one field of one item is one step however many keys it takes
// (the steps merge while their key is the same), until another item is
// chosen or another change, Undo or Redo comes between.

#include <string>
#include <utility>
#include <vector>

namespace hikari::application::visual {

// A rename: an item's name before and after a step, so a choice held
// outside the window (the toolbar's) follows the item.
struct Renamed {
    std::u16string from, to;
};
// What a move through the history renamed, in order: a name the caller
// holds goes through each in turn.
using Renames = std::vector<Renamed>;

template <class T>
class EditHistory {
public:
    struct State {
        std::vector<T> items;
        int selection = 0;
    };

    EditHistory(std::vector<T> items, int selection) : m_state{std::move(items), selection}, m_opened(m_state) {}

    const std::vector<T> &items() const { return m_state.items; }
    int selection() const { return m_state.selection; }
    // Another item chosen: no step, and the next change starts its own.
    void select(int selection)
    {
        m_state.selection = selection;
        m_mergeKey.clear();
    }

    // A change to `items` and `selection` named `step` (the window's word
    // for it), renaming as `renamed` says. A non-empty `mergeKey` equal to
    // the last step's folds it in.
    void change(std::vector<T> items, int selection, int step, std::string mergeKey = {}, Renames renamed = {})
    {
        m_moved = renamed;
        if (!mergeKey.empty() && mergeKey == m_mergeKey && !m_undo.empty()) {
            m_state = {std::move(items), selection};
            Entry &last = m_undo.back();
            last.other = selection;
            last.renamed.insert(last.renamed.end(), renamed.begin(), renamed.end());
            return;
        }
        m_undo.push_back({m_state, step, selection, std::move(renamed)});
        m_redo.clear();
        m_state = {std::move(items), selection};
        m_mergeKey = std::move(mergeKey);
    }
    // The state the window opened with, as one step (its ⋯ menu's "Undo all
    // changes"); false when nothing differs. Its renames undo every step's.
    bool undoAll(int step)
    {
        if (m_state.items == m_opened.items)
            return false;
        Renames back;
        for (auto e = m_undo.rbegin(); e != m_undo.rend(); ++e)
            appendUndone(back, e->renamed);
        change(m_opened.items, m_opened.selection, step, {}, std::move(back));
        return true;
    }
    // What the last change, Undo, Redo or Undo all renamed.
    const Renames &renamed() const { return m_moved; }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    // The step Undo or Redo would undo or do again (-1: none).
    int undoStep() const { return m_undo.empty() ? -1 : m_undo.back().step; }
    int redoStep() const { return m_redo.empty() ? -1 : m_redo.back().step; }
    bool undo()
    {
        if (m_undo.empty())
            return false;
        Entry e = std::move(m_undo.back());
        m_undo.pop_back();
        m_moved.clear();
        appendUndone(m_moved, e.renamed);
        // Redo brings back the step's own selection, not one chosen since.
        m_redo.push_back({{std::move(m_state.items), e.other}, e.step, e.state.selection, std::move(e.renamed)});
        m_state = std::move(e.state);
        m_mergeKey.clear();
        return true;
    }
    bool redo()
    {
        if (m_redo.empty())
            return false;
        Entry e = std::move(m_redo.back());
        m_redo.pop_back();
        m_moved = e.renamed;
        m_undo.push_back({{std::move(m_state.items), e.other}, e.step, e.state.selection, std::move(e.renamed)});
        m_state = std::move(e.state);
        m_mergeKey.clear();
        return true;
    }
    // A field left: the next change to it is a step of its own.
    void endMerge() { m_mergeKey.clear(); }

private:
    struct Entry {
        State state; // before the step (undo) or after it (redo)
        int step = 0;
        int other = 0; // the selection on the step's other side
        Renames renamed; // the step's, in order
    };
    static void appendUndone(Renames &out, const Renames &renamed)
    {
        for (auto r = renamed.rbegin(); r != renamed.rend(); ++r)
            out.push_back({r->to, r->from});
    }
    State m_state;
    State m_opened;
    std::vector<Entry> m_undo;
    std::vector<Entry> m_redo;
    std::string m_mergeKey;
    Renames m_moved;
};

} // namespace hikari::application::visual
