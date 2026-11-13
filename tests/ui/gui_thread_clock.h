#pragma once

#include <memory>

class GuiThreadClock {
public:
    // MUST KILL: the constructor captures the calling thread, so it must be built on the GUI thread
    GuiThreadClock();
    ~GuiThreadClock();
    GuiThreadClock(const GuiThreadClock &) = delete;
    GuiThreadClock &operator=(const GuiThreadClock &) = delete;
    // CPU time the captured thread has used, in milliseconds; -1 when the platform can't tell.
    double ms() const;

private:
    struct Thread;
    std::unique_ptr<Thread> m_thread;
};
