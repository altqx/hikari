#pragma once

// IndexedSource through the isolated FFMS2 media helper (N1; ADR 0016). One
// helper process per source; it is started on demand and replaced when it is
// lost. Runs on its owner's thread with a Qt event loop.

#include "hikari/application/indexed_source.h"
#include "hikari/backends/helper_host.h"

#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace hikari::backends {

class FfmsIndexedSource : public QObject, public application::IndexedSourcePort {
public:
    explicit FfmsIndexedSource(QString helperProgram, QObject *parent = nullptr);
    ~FfmsIndexedSource() override;

    std::uint64_t open(const std::string &path, Progress progress, Opened done) override;
    void cancelOpen() override;
    void frame(int index, FrameReady done) override;
    void openAudio(int track, AudioOpened done) override;
    void audio(std::int64_t start, std::int64_t count, AudioReady done) override;
    void cancelReads() override;
    std::uint64_t generation() const override { return m_generation; }

    // Tests: the helper process currently in use (null before the first open).
    helper::HelperHost *helperHost() const { return m_host.get(); }

private:
    void ensureHelper(std::function<void(bool)> ready);
    // Registers a read: the returned callback resolves it once (later calls
    // are ignored); cancelReads() resolves it as Cancelled.
    template <typename R> std::pair<std::uint64_t, std::function<void(R)>> track(std::function<void(R)> done);

    QString m_program;
    std::unique_ptr<helper::HelperHost> m_host;
    std::uint64_t m_generation = 0;
    bool m_open = false;
    std::optional<application::AudioInfo> m_audio;
    std::optional<std::uint64_t> m_openRequest;
    struct Read {
        std::uint64_t request = 0;
        std::function<void()> cancel;
    };
    std::map<std::uint64_t, Read> m_reads; // outstanding frame and audio requests
    std::uint64_t m_nextRead = 0;
};

} // namespace hikari::backends
