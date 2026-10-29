#pragma once

#include "hikari/application/edit_session.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

struct RecoveryContent {
    std::vector<std::byte> bytes;           // the committed content, in the Document's format
    std::string extension;                  // "ass", "srt" or "txt"
    std::string title;
    std::string originalPath;               // empty for an Untitled Document
    std::optional<std::size_t> draftRow;    // Document row of the pending draft
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
