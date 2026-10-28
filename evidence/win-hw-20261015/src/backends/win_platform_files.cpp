#include "hikari/backends/platform_files.h"
#include "hikari/backends/win_file_port.h"
#include "hikari/backends/win_file_reader.h"

namespace hikari::backends {

namespace {

class WinPlatformFilePort : public PlatformFilePort {
public:
    explicit WinPlatformFilePort(WriteCompletion completion) : m_port(std::move(completion)) {}
    void startWrite(application::PermitId permit, const application::DestinationKey &destination,
                    std::vector<std::byte> bytes) override
    {
        m_port.startWrite(permit, destination, std::move(bytes));
    }
    void requestCancel(application::PermitId permit) override { m_port.requestCancel(permit); }
    void waitIdle() override { m_port.waitIdle(); }

private:
    WinFilePort m_port;
};

} // namespace

std::unique_ptr<application::FileReadPort> makeFileReader()
{
    return std::make_unique<WinFileReader>();
}

std::unique_ptr<PlatformFilePort> makeFilePort(WriteCompletion completion)
{
    return std::make_unique<WinPlatformFilePort>(std::move(completion));
}

} // namespace hikari::backends
