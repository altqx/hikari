#pragma once

// Windows file reading for open, reload and external-change checks (A2).

#include "hikari/application/document_files.h"

namespace hikari::backends {

class WinFileReader : public application::FileReadPort {
public:
    std::expected<std::vector<std::byte>, application::ReadError>
    read(const application::DestinationKey &destination) override;
};

} // namespace hikari::backends
