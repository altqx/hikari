#pragma once

// Y9: MatroskaPort through the isolated FFMS2 media helper (its
// SubtitleTracks, Subtitles and Attachments requests; media_protocol.h). A
// helper process of its own, started at the first request and kept until it
// ends; a lost helper resolves the running request HelperLost and the next
// request starts a new one. Runs on its owner's thread with a Qt event loop.

#include "hikari/application/matroska.h"
#include "hikari/backends/helper_host.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace hikari::backends {

class FfmsMatroska : public QObject, public application::MatroskaPort {
    Q_OBJECT
public:
    explicit FfmsMatroska(QString helperProgram, QObject *parent = nullptr);
    ~FfmsMatroska() override;

    void subtitleTracks(const std::string &path, Tracks done) override;
    void subtitles(const std::string &path, int track, Progress progress, Read done) override;
    void attachments(const std::string &path, Attachments done) override;
    void cancel() override;

    // Tests: the helper process in use (null before the first request).
    helper::HelperHost *helperHost() const { return m_host.get(); }

private:
    using Handler = std::function<void(std::expected<helper::Event, helper::HostError>)>;
    // Starts the request once the helper is ready; `failed` resolves it when
    // the helper cannot start or the request cannot be sent.
    void send(std::vector<std::byte> payload, Handler handler, std::function<void(application::MatroskaError)> failed);
    void ensureHelper(std::function<void(bool)> ready);

    QString m_program;
    std::unique_ptr<helper::HelperHost> m_host;
    std::optional<std::uint64_t> m_request;
    std::function<void()> m_cancel; // resolves the running request as Cancelled
};

} // namespace hikari::backends
