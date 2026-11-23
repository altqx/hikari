#pragma once

// What a PluginProcess talks to: a hikari-plugin-helper over the helper framing, or a program plugin over JSON-RPC
// (ProgramPluginLink), which translates to and from the same plugin protocol payloads.

#include "hikari/backends/helper_host.h"

#include <QObject>

#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

namespace hikari::backends {

class PluginLink : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString name() const = 0;
    virtual void start() = 0;
    virtual bool ready() const = 0;
    virtual std::expected<std::uint64_t, helper::HostError> request(std::uint64_t run, std::vector<std::byte> payload,
                                                                    helper::HelperHost::Handler handler) = 0;
    virtual std::expected<std::uint64_t, helper::HostError> push(std::uint64_t run, std::vector<std::byte> payload,
                                                                 helper::HelperHost::Handler handler) = 0;
    // request 0 answers a call made outside any request (outsideService).
    virtual bool answer(std::uint64_t request, std::uint64_t call, helper::Outcome outcome,
                        std::vector<std::byte> payload = {}) = 0;
    // now: end it at once; otherwise it may first be asked to shut down.
    virtual void stop(bool now) = 0;
    virtual QByteArray diagnostics() const = 0;
    virtual qint64 processId() const = 0;

signals:
    void started();
    void refused(const QString &message);
    void lost();
    // A Service payload, as the helper framing carries it, sent while no request was open.
    void outsideService(std::uint64_t call, const std::vector<std::byte> &payload);
};

class HelperPluginLink : public PluginLink {
    Q_OBJECT
public:
    explicit HelperPluginLink(std::unique_ptr<helper::HelperHost> host, QObject *parent = nullptr);
    ~HelperPluginLink() override;

    QString name() const override { return QStringLiteral("hikari-plugin-helper"); }
    void start() override { m_host->start(); }
    bool ready() const override { return m_host->state() == helper::HelperHost::State::Ready; }
    std::expected<std::uint64_t, helper::HostError> request(std::uint64_t run, std::vector<std::byte> payload,
                                                            helper::HelperHost::Handler handler) override
    {
        return m_host->request(run, std::move(payload), std::move(handler));
    }
    std::expected<std::uint64_t, helper::HostError> push(std::uint64_t run, std::vector<std::byte> payload,
                                                         helper::HelperHost::Handler handler) override
    {
        return m_host->push(run, std::move(payload), std::move(handler));
    }
    bool answer(std::uint64_t request, std::uint64_t call, helper::Outcome outcome, std::vector<std::byte> payload) override
    {
        return m_host->answer(request, call, outcome, std::move(payload));
    }
    void stop(bool) override { m_host->stop(); }
    QByteArray diagnostics() const override { return m_host->diagnostics(); }
    qint64 processId() const override { return m_host->processId(); }

private:
    std::unique_ptr<helper::HelperHost> m_host;
};

}
