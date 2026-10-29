#pragma once

#include "hikari/application/document_files.h"

namespace hikari::backends {

class PosixFileReader : public application::FileReadPort {
public:
    std::expected<std::vector<std::byte>, application::ReadError>
    read(const application::DestinationKey &destination) override;
};

} // namespace hikari::backends
