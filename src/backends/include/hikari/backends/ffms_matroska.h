#pragma once

// A lost helper fails the running request with HelperLost; the next request starts a new
// one. Needs a Qt event loop on the owner's thread.

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

    // Null before the first request.
    helper::HelperHost *helperHost() const { return m_host.get(); }

private:
    using Handler = std::function<void(std::expected<helper::Event, helper::HostError>)>;
    std::shared_ptr<bool> begin(std::function<void()> resolveCancelled);
    void send(std::shared_ptr<bool> cancelled, std::vector<std::byte> payload, Handler handler,
              std::function<void(application::MatroskaError)> failed);
    void ensureHelper(std::function<void(bool)> ready);

    QString m_program;
    std::unique_ptr<helper::HelperHost> m_host;
    std::optional<std::uint64_t> m_request;
    std::function<void()> m_cancel;
};

} // namespace hikari::backends
