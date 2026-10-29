#include "hikari/backends/mecab_readings.h"

#include <QString>

#include <mutex>

#include <mecab.h>

namespace hikari::backends {
namespace {

// UniDic 2.1.2's kana form, the reading as written.
constexpr std::size_t kKanaField = 17;

// MeCab features are CSV; a field holding a comma is quoted.
std::string_view featureField(std::string_view feature, std::size_t index)
{
    std::size_t field = 0;
    for (std::size_t i = 0; i <= feature.size();) {
        const bool quoted = i < feature.size() && feature[i] == '"';
        const std::size_t start = quoted ? i + 1 : i;
        const std::size_t end = quoted ? feature.find('"', start) : feature.find(',', start);
        const std::size_t stop = end == std::string_view::npos ? feature.size() : end;
        if (field == index)
            return feature.substr(start, stop - start);
        i = quoted && stop < feature.size() ? stop + 2 : stop + 1;
        ++field;
    }
    return {};
}

std::u16string utf16(std::string_view utf8)
{
    return QString::fromUtf8(utf8.data(), qsizetype(utf8.size())).toStdU16String();
}

}

struct MecabReadings::State {
    std::unique_ptr<MeCab::Model> model;
    std::unique_ptr<MeCab::Tagger> tagger;
    std::mutex mutex;
};

MecabReadings::MecabReadings(std::unique_ptr<State> state) : m_state(std::move(state)) {}
MecabReadings::~MecabReadings() = default;

std::unique_ptr<MecabReadings> MecabReadings::open(const std::filesystem::path &folder, std::string *error)
{
    // argv, not one command line, so a folder may hold spaces; MeCab takes UTF-8 paths.
    const std::u8string dir = folder.u8string(), rc = (folder / u8"mecabrc").u8string();
    std::string d(dir.begin(), dir.end()), r(rc.begin(), rc.end());
    std::string program = "mecab", dFlag = "-d", rFlag = "-r";
    char *argv[] = {program.data(), dFlag.data(), d.data(), rFlag.data(), r.data()};
    auto state = std::make_unique<State>();
    state->model.reset(MeCab::createModel(int(std::size(argv)), argv));
    if (state->model)
        state->tagger.reset(state->model->createTagger());
    if (!state->model || !state->tagger) {
        if (error)
            *error = MeCab::getLastError();
        return nullptr;
    }
    return std::unique_ptr<MecabReadings>(new MecabReadings(std::move(state)));
}

std::vector<application::ReadingWord> MecabReadings::words(std::u16string_view text) const
{
    const QByteArray utf8 = QString::fromUtf16(text.data(), qsizetype(text.size())).toUtf8();
    std::vector<application::ReadingWord> out;
    const std::unique_ptr<MeCab::Lattice> lattice(m_state->model->createLattice());
    lattice->set_sentence(utf8.constData(), std::size_t(utf8.size()));
    {
        const std::lock_guard lock(m_state->mutex);
        if (!m_state->tagger->parse(lattice.get()))
            return out;
    }
    for (const MeCab::Node *node = lattice->bos_node(); node; node = node->next) {
        if (node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE)
            continue;
        application::ReadingWord word{utf16({node->surface, node->length}), {}};
        if (node->stat != MECAB_UNK_NODE)
            if (const auto kana = featureField(node->feature, kKanaField); kana != "*")
                word.reading = utf16(kana);
        out.push_back(std::move(word));
    }
    return out;
}

application::ReadingAnalyzer MecabReadings::analyzer(std::shared_ptr<const MecabReadings> readings)
{
    return [readings = std::move(readings)](std::u16string_view text) { return readings->words(text); };
}

}
