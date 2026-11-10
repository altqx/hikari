#pragma once

#include "hikari/application/aegisub_frame_times.h"
#include "hikari/application/automation.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/extents_cache.h"
#include "hikari/backends/helper_protocol.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::lua_host {

struct HostFailure {
    backends::helper::Outcome outcome = backends::helper::Outcome::Failed;
};

// nullopt: the host answered Unavailable. Throws HostFailure when it could not answer.
using Ask = std::function<std::optional<application::HostServiceReply>(application::HostServiceRequest)>;

// The app's text measure over IPC; an empty text is zero without asking. A batch the host can't answer goes
// text by text from then on.
class HostTextMeasure final : public application::TextMeasurePort {
public:
    explicit HostTextMeasure(Ask ask) : m_ask(std::move(ask)) {}
    std::optional<application::TextExtents> measure(const std::vector<std::string> &style,
                                                    const std::string &text) override;
    std::vector<std::optional<application::TextExtents>> measureMany(const std::vector<std::string> &style,
                                                                     const std::vector<std::string> &texts) override;

private:
    Ask m_ask;
    bool m_batchUnavailable = false;
};

// The video's frame times for one run, fetched on first use.
class HostFrameTimes {
public:
    explicit HostFrameTimes(Ask ask) : m_ask(std::move(ask)) {}
    const std::optional<application::AegisubFrameTimes> &times();

private:
    Ask m_ask;
    std::optional<std::optional<application::AegisubFrameTimes>> m_times;
};

// The running request's caches, shared by every helper call; beginRun (a Load or a Run) starts them afresh.
void beginRun(Ask ask);
application::karaoke_layout::ExtentsCache &runExtents();
HostFrameTimes &runFrameTimes();

}
