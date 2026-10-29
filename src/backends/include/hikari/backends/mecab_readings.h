#pragma once

#include "hikari/application/furigana.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends {

class MecabReadings {
public:
    // Null, with MeCab's reason in `error`, when the UniDic dictionary in `folder` does not load.
    static std::unique_ptr<MecabReadings> open(const std::filesystem::path &folder, std::string *error = nullptr);
    ~MecabReadings();
    MecabReadings(const MecabReadings &) = delete;
    MecabReadings &operator=(const MecabReadings &) = delete;

    // Readings are the written kana form (エイエン, not エーエン); unknown words get none.
    // Thread-safe.
    std::vector<application::ReadingWord> words(std::u16string_view text) const;

    static application::ReadingAnalyzer analyzer(std::shared_ptr<const MecabReadings> readings);

private:
    struct State;
    explicit MecabReadings(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

} // namespace hikari::backends
