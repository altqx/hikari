#pragma once

// IndexedSource through the isolated FFMS2 media helper (N1; ADR 0016). One
// helper process per source, started by open(). When the helper is lost, its
// generation is closed: every pending and later request resolves HelperLost
// until an explicit open() or restart() starts a new helper (I6). Runs on its
// owner's thread with a Qt event loop.

#include "hikari/application/general_player.h"
#include "hikari/application/indexed_source.h"
#include "hikari/backends/helper_host.h"

#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace hikari::backends {

// It also lists container chapters for general playback (N5), so FFmpeg
// stays out of the application process.
class FfmsIndexedSource : public QObject, public application::IndexedSourcePort, public application::ChapterPort {
    Q_OBJECT
public:
    explicit FfmsIndexedSource(QString helperProgram, QObject *parent = nullptr);
    ~FfmsIndexedSource() override;

    std::uint64_t open(const std::string &path, Progress progress, Opened done) override;
    void cancelOpen() override;
    void frame(int index, FrameReady done) override;
    void openAudio(int track, AudioOpened done) override;
    void audio(std::int64_t start, std::int64_t count, AudioReady done) override;
    std::uint64_t openDisplayAudio(const std::string &path, Progress progress, AudioOpened done) override;
    void cancelReads() override;
    void beginPcm(std::int64_t start, std::int64_t count, int outRate, int outChannels, PcmBegun done) override;
    void nextPcm(std::int64_t maxFrames, PcmReady done) override;
    void chapters(const std::string &path, Listed done) override;
    std::uint64_t generation() const override { return m_generation; }

    // Reopens the last opened path in a new helper (a new session); returns
    // its generation, or 0 (and NotOpen) when nothing was opened.
    std::uint64_t restart(Progress progress, Opened done);
    bool isHelperLost() const { return m_lost; }
    // Milliseconds from starting the latest helper to its handshake.
    double lastHelperStartupMs() const { return m_startupMs; }

    // Tests: the helper process currently in use (null before the first open).
    helper::HelperHost *helperHost() const { return m_host.get(); }

signals:
    // The helper ended; the generation is closed until open() or restart().
    void helperLost(quint64 generation);

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
    std::string m_path;
    bool m_lost = false;
    int m_pcmChannels = 0; // channels of the current PCM stream
    double m_startupMs = 0;
    struct Read {
        std::uint64_t request = 0;
        std::function<void()> cancel;
    };
    std::map<std::uint64_t, Read> m_reads; // outstanding frame and audio requests
    std::uint64_t m_nextRead = 0;
};

} // namespace hikari::backends
