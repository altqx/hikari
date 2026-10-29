#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application::visual {

struct Renamed {
    std::u16string from, to;
};
// Apply in order.
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
    // No step; ends merging.
    void select(int selection)
    {
        m_state.selection = selection;
        m_mergeKey.clear();
    }

    // A non-empty `mergeKey` equal to the last step's merges this change into it.
    void change(std::vector<T> items, int selection, int step, std::string mergeKey = {}, Renames renamed = {})
    {
        m_moved = renamed;
        ++m_revision;
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
    // Returns to the opened state as one step; false when nothing differs.
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
    // Bumps on every change, Undo and Redo; unchanged means renamed() is stale.
    std::uint64_t revision() const { return m_revision; }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    // -1 when none.
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
        ++m_revision;
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
        ++m_revision;
        m_undo.push_back({{std::move(m_state.items), e.other}, e.step, e.state.selection, std::move(e.renamed)});
        m_state = std::move(e.state);
        m_mergeKey.clear();
        return true;
    }
    void endMerge() { m_mergeKey.clear(); }

private:
    struct Entry {
        State state; // before the step (undo stack) or after it (redo stack)
        int step = 0;
        int other = 0; // the selection on the step's other side
        Renames renamed;
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
    std::uint64_t m_revision = 0;
};

} // namespace hikari::application::visual
