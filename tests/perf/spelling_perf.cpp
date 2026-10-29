#include "perf_harness.h"

#include "hikari/application/spell_checker.h"
#include "hikari/backends/legacy_spelling.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace hikari;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

const std::vector<std::u16string> kEnglish{u"the", u"quick", u"brown", u"fox", u"jumps", u"over", u"lazy", u"dog",
                                           u"you", u"know", u"what", u"happened", u"yesterday", u"night", u"we",
                                           u"should", u"leave", u"before", u"they", u"come", u"back", u"teh",
                                           u"wrold", u"recieve"};
const std::vector<std::u16string> kPolish{u"nie", u"wiem", u"co", u"się", u"stało", u"wczoraj", u"dziękuję", u"bardzo",
                                          u"musimy", u"iść", u"zanim", u"wrócą", u"dziekuje", u"wczorja"};
const std::vector<std::u16string> kKorean{u"안녕하세요", u"감사합니다", u"사랑해요", u"괜찮아요", u"어제", u"무슨",
                                          u"일이", u"있었어요", u"안녕하세묘"};
const std::vector<std::u16string> kTamil{u"வணக்கம்", u"நன்றி", u"நேற்று", u"என்ன", u"நடந்தது", u"வணக்கமம்"};
const std::vector<std::u16string> kThai{u"สวัสดี", u"ขอบคุณ", u"เมื่อวาน", u"เกิด", u"อะไร", u"ขึ้น"};
const std::vector<std::u16string> kSyllables{u"ka", u"zu", u"ki", u"ta", u"ke", u"shi", u"mi", u"no", u"ru", u"ha",
                                             u"ya", u"to", u"ri", u"se", u"na"};

std::vector<std::u16string> lines(std::size_t count)
{
    std::mt19937 random(474);
    auto pick = [&](const std::vector<std::u16string> &words) { return words[random() % words.size()]; };
    std::vector<std::u16string> out;
    for (std::size_t i = 0; i < count; ++i) {
        std::u16string name;
        for (int s = 0; s < 3; ++s)
            name += pick(kSyllables);
        name[0] = static_cast<char16_t>(name[0] - 32);
        std::u16string line = (i % 7 == 0 ? u"{\\i1}" : u"") + name + u",";
        const auto kind = random() % 20;
        const auto &other = kind < 4 ? kPolish : kind < 6 ? kKorean : kind < 7 ? kTamil : kind < 8 ? kThai : kEnglish;
        for (int w = 0; w < 8; ++w)
            line += u" " + pick(w % 3 == 2 ? kEnglish : other);
        out.push_back(line + u"!\\N" + pick(kEnglish) + u" " + pick(other) + u".");
    }
    return out;
}

double ms(Clock::duration d)
{
    return std::chrono::duration<double, std::milli>(d).count();
}

perf::BenchmarkResult once(std::string name, double milliseconds)
{
    perf::BenchmarkResult r;
    r.name = std::move(name);
    perf::RunStats s;
    s.operations = 1;
    s.p50Us = s.p95Us = s.p99Us = s.maxUs = s.meanUs = milliseconds * 1000;
    r.runs.push_back(s);
    return r;
}

}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: hikari_spelling_perf <report.json> <reference-hosts.json> [dictionary folder]\n";
        return 2;
    }
    const fs::path folder = argc > 3 ? fs::path(argv[3]) : fs::path(HIKARI_SPELLING_FIXTURES);
    const std::u16string primary = fs::exists(folder / "en_US.dic") ? u"en_US" : u"en_TEST";
    const auto text = backends::legacySpellingText(true);
    const auto file = lines(5000);
    std::vector<perf::BenchmarkResult> results;

    for (const auto &symbol : application::availableDictionaries(folder)) {
        const fs::path base = folder / fs::path(symbol);
        const auto start = Clock::now();
        const auto backend =
            backends::hunspellSpellingLoader()(fs::path(base).concat(".aff"), fs::path(base).concat(".dic"));
        results.push_back(once("spelling.load." + fs::path(symbol).string(), ms(Clock::now() - start)));
    }

    application::SpellChecker grid(folder, backends::hunspellSpellingLoader());
    auto start = Clock::now();
    if (grid.initialize(primary) != application::SpellChecker::Status::Ready)
        return 1;
    results.push_back(once("spelling.start", ms(Clock::now() - start)));

    std::size_t marks = 0;
    auto pass = [&](application::SpellChecker &checker, bool wait) {
        marks = 0;
        for (const auto &line : file)
            marks += core::legacy::checkTextAndBrackets(line, core::SubtitleFormat::Ass, text.segment,
                                                        checker.wordCheck(wait))
                         .misspells.size();
    };
    start = Clock::now();
    pass(grid, false);
    results.push_back(once("spelling.first_pass_5000", ms(Clock::now() - start)));
    const std::size_t gridMarks = marks;

    application::SpellChecker checker(folder, backends::hunspellSpellingLoader());
    if (checker.initialize(primary) != application::SpellChecker::Status::Ready)
        return 1;
    start = Clock::now();
    pass(checker, true);
    results.push_back(once("spelling.first_pass_waiting_5000", ms(Clock::now() - start)));
    const std::size_t firstMarks = marks;
    double cached = 1e300;
    for (int i = 0; i < 5; ++i) {
        start = Clock::now();
        pass(checker, false);
        cached = std::min(cached, ms(Clock::now() - start));
    }
    results.push_back(once("spelling.cached_pass_5000", cached));
    std::size_t at = 0;
    results.push_back(perf::run({"spelling.cached_line", 5, 1000, perf::kWarmWarmup, [&] {
        const auto m = core::legacy::checkTextAndBrackets(file[at++ % file.size()], core::SubtitleFormat::Ass,
                                                          text.segment, checker.wordCheck());
        if (m.errors.size() > 1000)
            std::abort();
    }}));

    const auto host = perf::currentHost();
    const bool calibrated = perf::isCalibrated(host, argv[2]);
    if (!perf::writeReport(argv[1], host, calibrated, results))
        return 1;
    std::cout << "dictionaries: " << folder.string() << "\n";
    for (const auto &r : results) {
        std::cout << r.name << ": p50/p95/max per run (ms):";
        for (const auto &s : r.runs)
            std::cout << ' ' << s.p50Us / 1000 << '/' << s.p95Us / 1000 << '/' << s.maxUs / 1000;
        std::cout << "\n";
    }
    std::cout << "marked words, first pass: " << gridMarks << ", waiting: " << firstMarks << ", cached pass: " << marks
              << "\n";
    std::cout << (calibrated ? "calibrated host\n" : "uncalibrated host: observations only\n");
    return 0;
}
