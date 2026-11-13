#pragma once

#include "hikari/application/clash_choice.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// ADR 0030, "Shared things with a Project tier".
enum class Tier { Project, Mine };
enum class TierRule { HiddenByName, Merged, PerKey };

struct TieredName {
    std::u8string name;
    Tier tier = Tier::Mine;
    // A Mine item that the Project's item of the same name hides.
    bool hidden = false;
    bool operator==(const TieredName &) const = default;
};

// The Project's names, then the user's, each in its own order.
std::vector<TieredName> hideByName(const std::vector<std::u8string> &project, const std::vector<std::u8string> &mine);
// The Project's tier when it has `name`.
std::optional<Tier> resolveByName(const std::vector<std::u8string> &project, const std::vector<std::u8string> &mine,
                                  const std::u8string &name);

// The name an item copied into a tier lands under, or none when the clash is skipped. A Rename
// already taken gets "<new> (2)"...
std::optional<std::u8string> landingName(const std::vector<std::u8string> &taken, const std::u8string &name,
                                         const ClashChoice &choice);

template <class Str> Str numberedName(const Str &base, int n)
{
    const std::string digits = std::to_string(n);
    Str out = base;
    out.push_back(' ');
    out.push_back('(');
    for (const char c : digits)
        out.push_back(static_cast<typename Str::value_type>(c));
    out.push_back(')');
    return out;
}

// landingName for any name type; `same` compares two names (folding case where the list does).
template <class Str, class Same>
std::optional<Str> landingNameBy(const std::vector<Str> &taken, const Str &name, const ClashChoice &choice,
                                 const Str &newName, Same same)
{
    const auto isTaken = [&](const Str &n) {
        return std::any_of(taken.begin(), taken.end(), [&](const Str &t) { return same(t, n); });
    };
    if (!isTaken(name))
        return name;
    if (choice.action == ClashAction::Replace)
        return name;
    if (choice.action == ClashAction::Skip)
        return std::nullopt;
    const Str base = newName.empty() ? name : newName;
    if (!isTaken(base))
        return base;
    for (int n = 2;; ++n)
        if (const Str next = numberedName(base, n); !isTaken(next))
            return next;
}

// Carried by each item of a hidden-by-name list.
struct TierMark {
    Tier tier = Tier::Mine;
    // A Mine item that a Project item of the same name hides.
    bool hidden = false;
    bool operator==(const TierMark &) const = default;
};

// Items have a `TierMark mark`; `name` reads an item's name (a member pointer works) and `same` compares two names.
template <class Item, class Name, class Same> void markHidden(std::vector<Item> &all, Name name, Same same)
{
    for (auto &item : all)
        item.mark.hidden = item.mark.tier == Tier::Mine &&
                           std::any_of(all.begin(), all.end(), [&](const Item &other) {
                               return other.mark.tier == Tier::Project && same(std::invoke(name, other), std::invoke(name, item));
                           });
}

// The Project's items, then the user's, marked.
template <class Item, class Name, class Same>
std::vector<Item> combineTiers(std::vector<Item> project, std::vector<Item> mine, Name name, Same same)
{
    for (auto &item : project)
        item.mark.tier = Tier::Project;
    for (auto &item : mine)
        item.mark.tier = Tier::Mine;
    project.insert(project.end(), std::make_move_iterator(mine.begin()), std::make_move_iterator(mine.end()));
    markHidden(project, name, same);
    return project;
}

template <class Item> std::vector<Item> itemsIn(const std::vector<Item> &all, Tier tier)
{
    std::vector<Item> out;
    for (const auto &item : all)
        if (item.mark.tier == tier)
            out.push_back(item);
    return out;
}

template <class Item> std::vector<Item> visibleItems(const std::vector<Item> &all)
{
    std::vector<Item> out;
    for (const auto &item : all)
        if (!item.mark.hidden)
            out.push_back(item);
    return out;
}

// Where a new item of `tier` goes: after the Project's last item, or at the end.
template <class Item> std::size_t tierEnd(const std::vector<Item> &all, Tier tier)
{
    if (tier == Tier::Mine)
        return all.size();
    return static_cast<std::size_t>(
        std::count_if(all.begin(), all.end(), [](const Item &item) { return item.mark.tier == Tier::Project; }));
}

// The item visible under `n`: the Project's, else the user's.
template <class Item, class Str, class Name, class Same>
std::optional<std::size_t> resolveItem(const std::vector<Item> &all, const Str &n, Name name, Same same)
{
    std::optional<std::size_t> mine;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!same(std::invoke(name, all[i]), n))
            continue;
        if (all[i].mark.tier == Tier::Project)
            return i;
        if (!mine)
            mine = i;
    }
    return mine;
}

constexpr Tier otherTier(Tier tier)
{
    return tier == Tier::Project ? Tier::Mine : Tier::Project;
}

// The other tier's item with all[index]'s name, when there is one.
template <class Item, class Name, class Same>
std::optional<std::size_t> otherTierClash(const std::vector<Item> &all, std::size_t index, Name name, Same same)
{
    if (index >= all.size())
        return std::nullopt;
    const Tier to = otherTier(all[index].mark.tier);
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].mark.tier == to && same(std::invoke(name, all[i]), std::invoke(name, all[index])))
            return i;
    return std::nullopt;
}

// Copies all[index] into the other tier under the clash's landing name, or moves it there with
// `move`; returns its index after the change, none when skipped. `rename(item, name)` writes a name.
template <class Item, class Str, class Name, class Rename, class Same>
std::optional<std::size_t> sendToOtherTier(std::vector<Item> &all, std::size_t index, const ClashChoice &choice,
                                           const Str &newName, bool move, Name name, Rename rename, Same same)
{
    if (index >= all.size())
        return std::nullopt;
    const Tier to = otherTier(all[index].mark.tier);
    std::vector<Str> taken;
    for (const auto &item : all)
        if (item.mark.tier == to)
            taken.push_back(Str(std::invoke(name, item)));
    const auto landed = landingNameBy(taken, Str(std::invoke(name, all[index])), choice, newName, same);
    if (!landed)
        return std::nullopt;
    Item copy = all[index];
    copy.mark = {to, false};
    std::invoke(rename, copy, *landed);
    std::size_t at = 0;
    const auto replaced = std::find_if(all.begin(), all.end(),
                                       [&](const Item &item) { return item.mark.tier == to && same(std::invoke(name, item), *landed); });
    if (replaced != all.end()) {
        at = static_cast<std::size_t>(replaced - all.begin());
        all[at] = std::move(copy);
    } else {
        at = tierEnd(all, to);
        all.insert(all.begin() + static_cast<std::ptrdiff_t>(at), std::move(copy));
        if (at <= index)
            ++index;
    }
    if (move) {
        all.erase(all.begin() + static_cast<std::ptrdiff_t>(index));
        if (index < at)
            --at;
    }
    markHidden(all, name, same);
    return at;
}

struct TierStore {
    std::string_view id;
    // None for a user-only store.
    std::optional<TierRule> rule;
    // Relative to the Project folder; empty for a user-only store.
    std::string_view projectPath;
};
std::span<const TierStore> tierStores();
const TierStore *tierStore(std::string_view id);
// Where the store keeps its Project copy; none for a user-only store or without a Project.
std::optional<std::filesystem::path> projectPathOf(std::string_view id, const std::filesystem::path &projectRoot);

}
