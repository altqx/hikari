#pragma once

// KA4 (#279): the furigana suggestions' analyser, MeCab (ports/mecab) over
// UniDic 2.1.2 (ports/hikari-unidic-lite, shipped as Dictionary/unidic-lite),
// behind application::ReadingAnalyzer.

#include "hikari/application/furigana.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends {

class MecabReadings {
public:
    // The UniDic dictionary in `folder` (sys.dic, matrix.bin, char.bin,
    // unk.dic, dicrc and the mecabrc read instead of the system's). Null,
    // with MeCab's reason in `error`, when it does not load.
    static std::unique_ptr<MecabReadings> open(const std::filesystem::path &folder, std::string *error = nullptr);
    ~MecabReadings();
    MecabReadings(const MecabReadings &) = delete;
    MecabReadings &operator=(const MecabReadings &) = delete;

    // The words of `text` with UniDic's kana form (its feature 17): the
    // reading as written, not as pronounced (エイエン, not エーエン). A word
    // the dictionary does not know has no reading. Safe from any thread.
    std::vector<application::ReadingWord> words(std::u16string_view text) const;

    // words() as the suggestions' analyser; the analyser keeps this alive.
    static application::ReadingAnalyzer analyzer(std::shared_ptr<const MecabReadings> readings);

private:
    struct State;
    explicit MecabReadings(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

} // namespace hikari::backends
