#pragma once

#include "hikari/application/display_audio_port.h"
#include "hikari/application/general_player.h"
#include "hikari/application/indexed_source.h"
#include "hikari/backends/helper_host.h"

#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace hikari::backends {

// One helper process per source. Once the helper is lost, every request resolves HelperLost
// until open() or restart(). Needs its owner thread's Qt event loop.
class FfmsIndexedSource : public QObject,
                          public application::IndexedSourcePort,
                          public application::ChapterPort,
                          public application::DisplayAudioPort {
    Q_OBJECT
public:
    using Progress = application::IndexedSourcePort::Progress;
    using Opened = application::IndexedSourcePort::Opened;

    explicit FfmsIndexedSource(QString helperProgram, QObject *parent = nullptr);
    ~FfmsIndexedSource() override;

    std::uint64_t open(const std::string &path, Progress progress, Opened done) override;
    std::uint64_t openIndexed(const std::string &path, const application::IndexRequest &index, Progress progress,
                              Opened done) override;
    void cancelOpen() override;
    std::optional<application::OpenFailure> openFailure() const override { return m_openFailure; }
    void frame(int index, FrameReady done) override;
    void openAudio(int track, AudioOpened done) override;
    void audio(std::int64_t start, std::int64_t count, AudioReady done) override;
    void cancelReads() override;
    void close() override;
    void setInputMatrix(int colorSpace, int colorRange, MatrixSet done) override;
    void beginPcm(std::int64_t start, std::int64_t count, int outRate, int outChannels, PcmBegun done) override;
    void nextPcm(std::int64_t maxFrames, PcmReady done) override;
    void chapters(const std::string &path, Listed done) override;
    std::uint64_t generation() const override { return m_generation; }

    // openDisplayAudio starts a new generation like open(); the audio box needs its own instance.
    void probe(const std::string &path, Probed done) override;
    void openDisplayAudio(const std::string &path, int track, const std::string &indexFile, Progress progress,
                          application::DisplayAudioPort::Opened done) override;
    void displayAudio(const application::BlockRead &read, Read done) override;
    void cancelDisplay() override;
    void closeDisplay() override;

    // Removes the handoff index file once the audio box has read it, if it is still this source's.
    void releaseIndexHandoff(const std::string &handoffIndexFile);

    // Reopens the last path in a new helper; returns its generation, or 0 (NotOpen) when nothing
    // was opened.
    std::uint64_t restart(Progress progress, Opened done);
    bool isHelperLost() const { return m_lost; }
    double lastHelperStartupMs() const { return m_startupMs; }

    // Null before the first open.
    helper::HelperHost *helperHost() const { return m_host.get(); }

signals:
    void helperLost(quint64 generation);

private:
    void dropHandoff();
    void closeAll();
    void ensureHelper(std::function<void(bool)> ready);
    template <typename R> std::pair<std::uint64_t, std::function<void(R)>> track(std::function<void(R)> done);

    QString m_program;
    std::unique_ptr<helper::HelperHost> m_host;
    std::uint64_t m_generation = 0;
    bool m_open = false;
    std::optional<application::AudioInfo> m_audio;
    std::optional<std::uint64_t> m_openRequest;
    std::optional<application::OpenFailure> m_openFailure;
    std::string m_path;
    application::IndexRequest m_index;
    QString m_handoff;
    bool m_lost = false;
    int m_pcmChannels = 0;
    double m_startupMs = 0;
    struct Pending {
        std::uint64_t request = 0;
        std::function<void()> cancel;
    };
    std::map<std::uint64_t, Pending> m_reads;
    std::map<std::uint64_t, Pending> m_displayReads;
    std::uint64_t m_nextRead = 0;
    std::optional<application::AudioInfo> m_display;
    template <typename R>
    std::pair<std::uint64_t, std::function<void(R)>> trackDisplay(std::function<void(R)> done);
    void parseDisplayOpen(std::uint64_t generation, const helper::Event &e,
                          const std::function<void(std::expected<application::DisplayAudioOpened, application::AudioFailure>)> &done);
};

} // namespace hikari::backends
