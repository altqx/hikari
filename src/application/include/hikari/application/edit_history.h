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

// A step's rename: an item's name before and after it (empty for any other
// step), so a choice held outside the window follows the item.
struct Renamed {
    std::u16string from, to;
};

// A rename or a replace undone or done again, from the two lists: the one
// item whose name alone differs at the same place (Rename), or, with one
// item more on one side, the item that took the extra one's name (`same`:
// names equal ignoring case, as Replace compares them), the rest of it as
// it was (else it is a Delete). Else empty.
template <class T, class Same>
Renamed renamedBetween(const std::vector<T> &before, const std::vector<T> &after, Same same)
{
    const bool undoneReplace = after.size() == before.size() + 1;
    const std::vector<T> &longer = undoneReplace ? after : before;
    const std::vector<T> &shorter = undoneReplace ? before : after;
    if (longer.size() != shorter.size() && longer.size() != shorter.size() + 1)
        return {};
    const bool replace = longer.size() != shorter.size();
    for (std::size_t gone = 0; gone < (replace ? longer.size() : 1); ++gone) {
        // `longer` without `gone` (a Replace's removed item) against `shorter`.
        std::size_t differs = longer.size(), at = 0;
        bool one = true;
        for (std::size_t i = 0, j = 0; i < longer.size() && one; ++i) {
            if (replace && i == gone)
                continue;
            if (longer[i].name != shorter[j].name) {
                one = differs == longer.size();
                differs = i;
                at = j;
            }
            ++j;
        }
        if (!one || differs == longer.size() || (replace && !same(shorter[at].name, longer[gone].name)))
            continue;
        T renamed = longer[differs];
        renamed.name = shorter[at].name;
        if (!(renamed == shorter[at]))
            continue;
        if (undoneReplace)
            return {shorter[at].name, longer[differs].name};
        return {longer[differs].name, shorter[at].name};
    }
    return {};
}

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
    // for it). A non-empty `mergeKey` equal to the last step's folds it in.
    void change(std::vector<T> items, int selection, int step, std::string mergeKey = {})
    {
        if (!mergeKey.empty() && mergeKey == m_mergeKey && !m_undo.empty()) {
            m_state = {std::move(items), selection};
            return;
        }
        m_undo.push_back({m_state, step});
        m_redo.clear();
        m_state = {std::move(items), selection};
        m_mergeKey = std::move(mergeKey);
    }
    // The state the window opened with, as one step (its ⋯ menu's "Undo all
    // changes"); false when nothing differs.
    bool undoAll(int step)
    {
        if (m_state.items == m_opened.items)
            return false;
        change(m_opened.items, m_opened.selection, step);
        return true;
    }

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
        m_redo.push_back({m_state, e.step});
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
        m_undo.push_back({m_state, e.step});
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
    };
    State m_state;
    State m_opened;
    std::vector<Entry> m_undo;
    std::vector<Entry> m_redo;
    std::string m_mergeKey;
};

} // namespace hikari::application::visual
