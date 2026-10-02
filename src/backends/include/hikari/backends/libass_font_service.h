#pragma once

// FontService through the pinned libass and its Hikari font diagnostics patch
// (N4, F47-abi). Each request renders in a fresh renderer, so its selections
// are not hidden by another request's font cache. Bytes are hashed as the
// renderer read them: from the provider's stream, or from the file the
// provider named. System faces are listed through the same provider libass
// uses (its static fontconfig on Linux, DirectWrite on Windows).

#include "hikari/application/font_service.h"

namespace hikari::backends {

class LibassFontService final : public application::FontServicePort {
public:
    std::expected<application::FontReport, application::FontError>
    resolve(const application::FontEnvironment &environment,
            const std::vector<application::FontRequest> &requests) override;
    std::vector<application::SystemFace> systemFaces() override;
};

} // namespace hikari::backends
