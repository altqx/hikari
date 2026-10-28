#pragma once

// Autosave recovery bundles (P3; accepted L58-recovery-copy and the
// 2026-09-29 retention choices). Each open Document with unsaved work gets a
// bundle of generations under the recovery directory: the committed content
// in its own format, the pending draft (kept pending on recovery, never
// committed), and what the Document was. A generation is written and
// verified before a manifest naming it replaces the previous one atomically;
// the last 3 (the legacy capacity, 0 disables autosave) are kept. A clean
// close after Save or an explicit Discard deletes the bundle; after a crash
// the bundles stay until recovered or dismissed, and generations older than
// 30 days are pruned. This is versioned application data, not the legacy
// Subs/ autosave grammar.

#include "hikari/application/edit_session.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

struct RecoveryContent {
    std::vector<std::byte> bytes;           // the committed content, encoded in its format
    std::string extension;                  // "ass", "srt" or "txt" (legacy save extension)
    std::string title;
    std::string originalPath;               // empty for an Untitled Document
    std::optional<std::size_t> draftRow;    // the pending draft's Line, by Document row
    DraftChange draft;
    std::optional<std::pair<std::int64_t, std::int64_t>> frameRate; // MicroDVD's own rate
    std::int64_t writtenMs = 0;             // milliseconds since the epoch
};

struct RecoveryBundle {
    std::string key;
    std::string session;
    std::vector<std::uint64_t> generations; // oldest first
    RecoveryContent latest;                 // the newest valid generation
};

class RecoveryStore {
public:
    static constexpr int kDefaultCapacity = 3;

    RecoveryStore(std::filesystem::path root, std::string session, int capacity = kDefaultCapacity);

    bool enabled() const { return m_capacity > 0 && !m_root.empty(); }
    // Legacy reads AUTOSAVE_MAX_FILES at each autosave.
    void setCapacity(int capacity) { m_capacity = capacity; }
    // A new generation for `key`; false (and the previous generation kept)
    // when it could not be written and verified.
    bool write(const std::string &key, const RecoveryContent &content);
    void discard(const std::string &key);
    // Bundles left by other sessions for which `sessionEnded` is true.
    std::vector<RecoveryBundle> leftovers(const std::function<bool(const std::string &session)> &sessionEnded) const;
    std::optional<RecoveryContent> read(const std::string &key, std::uint64_t generation) const;
    // Removes generations written before `now - maxAge` (bundles left empty go too).
    void prune(std::chrono::system_clock::time_point now, std::chrono::hours maxAge = std::chrono::hours(24 * 30));

private:
    std::optional<std::vector<std::uint64_t>> manifest(const std::filesystem::path &bundle, std::string *session) const;
    bool writeManifest(const std::filesystem::path &bundle, const std::vector<std::uint64_t> &generations) const;

    std::filesystem::path m_root;
    std::string m_session;
    int m_capacity;
};

} // namespace hikari::application
