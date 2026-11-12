#pragma once

#include "hikari/backends/ai_inline_run.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::backends::ai {

/// The Translate inline action as one Honya pipeline run in the sidecar (`_hikari/translate`): pre-pass, translator,
/// audit, reviewer with retries and a coherence sweep over every Line at once. Results arrive when the run ends.
class TranslationRun final : public LineRun {
public:
    struct Job {
        std::uint64_t key = 0;
        application::ai_inline::Prepared prepared;
        std::string speaker;
        std::int64_t startMs = 0;
        std::int64_t endMs = 0;
    };

    struct ProjectFile {
        std::string name;
        std::string data;
    };

    struct Wiring {
        InlineRun::Wiring lines;
        /// The Document whose project folder the sidecar reads through `translation.project`; none reads no folder.
        std::optional<std::uint64_t> document;
        std::string language;
        application::ai::ModelRef translator;
        application::ai::ModelRef reviewer;
        /// The translated Lines before the run, oldest first.
        std::vector<std::string> before;
        /// Lines the pipeline has finished, of all it was given.
        std::function<void(std::size_t done, std::size_t total)> progressed;
        /// The project files the run's approved Lines added to, before finished().
        std::function<void(std::vector<ProjectFile>)> projectChanged;
    };

    TranslationRun(std::vector<Job> jobs, Wiring wiring);
    ~TranslationRun() override;
    TranslationRun(const TranslationRun &) = delete;
    TranslationRun &operator=(const TranslationRun &) = delete;

    void start() override;
    void stop() override;
    void drop(std::uint64_t key) override;
    double cost() const override { return m_cost; }
    bool costKnown() const override { return m_costKnown; }
    bool running() const override { return m_running; }

private:
    void progress(const api::Json &params);
    void answered(std::expected<api::Json, application::ai::Error> result);
    void end(InlineRun::End why);

    std::vector<Job> m_jobs;
    Wiring m_wiring;
    std::vector<std::uint64_t> m_dropped;
    std::optional<application::ai::StreamId> m_stream;
    double m_cost = 0;
    bool m_costKnown = true;
    bool m_running = false;
    bool m_stopping = false;
    InlineRun::End m_stopReason = InlineRun::End::Stopped;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// The protocol's TranslateParams for these Lines, without their stream.
api::Json translateParams(const std::vector<TranslationRun::Job> &jobs, const TranslationRun::Wiring &wiring);

}
