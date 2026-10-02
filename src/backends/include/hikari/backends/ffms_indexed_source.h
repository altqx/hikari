#pragma once

// IndexedSource through the isolated FFMS2 media helper (N1; ADR 0016). One
// helper process per source; it is started on demand and replaced when it is
// lost. Runs on its owner's thread with a Qt event loop.

#include "hikari/application/indexed_source.h"
#include "hikari/backends/helper_host.h"

#include <QString>

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
    std::uint64_t generation() const override { return m_generation; }

    // Tests: the helper process currently in use (null before the first open).
    helper::HelperHost *helperHost() const { return m_host.get(); }

private:
    void ensureHelper(std::function<void(bool)> ready);

    QString m_program;
    std::unique_ptr<helper::HelperHost> m_host;
    std::uint64_t m_generation = 0;
    bool m_open = false;
    std::optional<std::uint64_t> m_openRequest;
};

} // namespace hikari::backends
