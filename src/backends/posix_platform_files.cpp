#include "hikari/backends/platform_files.h"
#include "hikari/backends/posix_file_port.h"
#include "hikari/backends/posix_file_reader.h"

namespace hikari::backends {

namespace {

class PosixPlatformFilePort : public PlatformFilePort {
public:
    explicit PosixPlatformFilePort(WriteCompletion completion) : m_port(std::move(completion)) {}
    void startWrite(application::PermitId permit, const application::DestinationKey &destination,
                    std::vector<std::byte> bytes) override
    {
        m_port.startWrite(permit, destination, std::move(bytes));
    }
    void requestCancel(application::PermitId permit) override { m_port.requestCancel(permit); }
    void waitIdle() override { m_port.waitIdle(); }

private:
    PosixFilePort m_port;
};

} // namespace

std::unique_ptr<application::FileReadPort> makeFileReader()
{
    return std::make_unique<PosixFileReader>();
}

std::unique_ptr<PlatformFilePort> makeFilePort(WriteCompletion completion)
{
    return std::make_unique<PosixPlatformFilePort>(std::move(completion));
}

} // namespace hikari::backends
