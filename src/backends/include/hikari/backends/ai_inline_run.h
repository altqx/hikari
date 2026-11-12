#pragma once

#include "hikari/api/json.h"
#include "hikari/application/ai_inline.h"
#include "hikari/application/ai_providers.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::backends::ai {

class AiRequests;

/// An inline action's engine: each Line's result, then finished(), once.
class LineRun {
public:
    virtual ~LineRun() = default;
    virtual void start() = 0;
    /// Cancels the running requests and drops the waiting ones; finished() follows with Stopped.
    virtual void stop() = 0;
    /// The Line leaves the run: waiting, it isn't sent; running, its answer is ignored.
    virtual void drop(std::uint64_t key) = 0;
    virtual double cost() const = 0;
    virtual bool costKnown() const = 0;
    virtual bool running() const = 0;
};

/// One inline action over many Lines: one `_hikari/complete` request per Line, a few at a time, with no tools.
class InlineRun final : public LineRun {
public:
    struct Job {
        std::uint64_t key = 0;
        application::ai_inline::Prepared prepared;
    };

    struct Result {
        std::uint64_t key = 0;
        application::ai_inline::Answer answer;
        std::optional<application::ai::Error> error; // the request failed; answer is unset
        /// A translation the pipeline's reviewer never approved, and what it found.
        bool needsReview = false;
        std::vector<std::string> issues;
    };

    enum class End { Done, Stopped, DailyCap, NoModel };

    struct Wiring {
        std::function<AiRequests *()> requests;
        std::function<const application::ai::Config *()> config;
        /// Adds a request's cost to the day's spending.
        std::function<void(double)> spend;
        /// True once the day's cap is reached; the run stops before its next request.
        std::function<bool()> overDailyCap;
        std::function<void(const Result &)> lineDone;
        std::function<void(End)> finished;
        std::size_t parallel = 4;
    };

    InlineRun(application::ai::Role role, std::vector<Job> jobs, Wiring wiring);
    ~InlineRun() override;
    InlineRun(const InlineRun &) = delete;
    InlineRun &operator=(const InlineRun &) = delete;

    void start() override;
    void stop() override;
    void drop(std::uint64_t key) override;

    std::size_t total() const { return m_total; }
    std::size_t done() const { return m_done; }
    double cost() const override { return m_cost; }
    bool costKnown() const override { return m_costKnown; }
    bool running() const override { return !m_ended; }

private:
    void pump();
    void stopFor(End why);
    void end(End why);
    void received(std::uint64_t key, const application::ai::Event &event);

    application::ai::Role m_role;
    std::deque<Job> m_waiting;
    std::map<std::uint64_t, application::ai::StreamId> m_running;
    std::map<std::uint64_t, application::ai_inline::Prepared> m_prepared;
    Wiring m_wiring;
    std::optional<application::ai::ModelRef> m_model;
    std::size_t m_total = 0;
    std::size_t m_done = 0;
    double m_cost = 0;
    bool m_costKnown = true;
    bool m_ended = false;
    bool m_stopping = false;
    End m_stopReason = End::Stopped;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// The protocol Request for one prepared Line, without its model.
api::Json inlineRequest(const application::ai_inline::Prepared &prepared);

}
