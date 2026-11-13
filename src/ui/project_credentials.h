#pragma once

#include "hikari/application/owned_file.h"

#include <QString>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::ui {

// A Project's AI keys: `.hikari/credentials.toml`, `version = 1` and a `[keys]` table of entry names to keys.
// `.hikari/.gitignore` keeps it out of git. Hikari AI never reads it.
class ProjectCredentials final {
public:
    static constexpr std::int64_t kVersion = 1;
    using Keys = std::map<std::string, std::string, std::less<>>;

    explicit ProjectCredentials(QString root);

    QString path() const;
    QString gitignorePath() const;
    bool exists() const;
    // Empty when the file is missing; a file that can't be read gives its problem.
    Keys keys(std::optional<application::ParseFailure> *problem = nullptr) const;

    // Writes the file with an empty entry for each of `entries`, private to the user where the system allows,
    // and adds the lines `.hikari/.gitignore` lacks. An existing file is left as it is.
    bool create(const std::vector<std::string> &entries) const;
    // Sets `entry` to `key`, creating the file first.
    bool put(std::string_view entry, std::string_view key) const;

private:
    bool ignoreInGit() const;

    QString m_root;
};

}
