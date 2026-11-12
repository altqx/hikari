#pragma once

#include "hikari/api/host.h"
#include "hikari/backends/external_aligner.h"

#include <QObject>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace hikari::app {

class AiKaraoke final : public QObject, public api::KaraokeHost {
public:
    struct Wiring {
        std::function<std::shared_ptr<const application::DisplayAudio>()> audio;
        std::function<application::ReadingAnalyzer()> readings;
        std::function<application::KaraokeTemplaterPort *()> templater;
        QString workRoot;
        std::chrono::milliseconds templaterTimeout{std::chrono::minutes(2)};
    };
    explicit AiKaraoke(Wiring wiring, QObject *parent = nullptr);
    ~AiKaraoke() override;

    backends::ExternalAligner &aligner() { return m_aligner; }

    application::KaraokeCharClass classes() const override;
    const application::Hyphenation *hyphenation() const override;
    application::ReadingAnalyzer readings() const override;
    void onsets(std::function<void(std::optional<std::vector<int>>)> done) override;
    application::KaraokeTemplaterPort *templater() override;
    void align(const api::AlignRequest &request, std::function<void(api::AlignResult)> done) override;

private:
    class Templater;
    struct Onsets;

    Wiring m_wiring;
    std::unique_ptr<Templater> m_templater;
    std::unique_ptr<Onsets> m_onsets;
    backends::ExternalAligner m_aligner;
};

}
