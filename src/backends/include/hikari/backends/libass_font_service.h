#pragma once

// FontService through the pinned libass and its Hikari font diagnostics patch
// (N4, F47-abi). Each request renders in a fresh renderer, so its selections
// are not hidden by another request's font cache. Bytes are hashed as the
// renderer read them: from the provider's stream, or from the file the
// provider named. System faces are listed through the same provider libass
// uses (its static fontconfig on Linux, DirectWrite on Windows).
//
// Y6: the font picker's faces add the environment's external fonts (read
// from their bytes), coverage answers the font filter, and refresh() makes
// the next listing read the installed fonts again (F47-refresh).

#include "hikari/application/font_service.h"

#include <atomic>

namespace hikari::backends {

class LibassFontService final : public application::FontServicePort {
public:
    std::expected<application::FontReport, application::FontError>
    resolve(const application::FontEnvironment &environment,
            const std::vector<application::FontRequest> &requests) override;
    std::vector<application::SystemFace> systemFaces() override;
    std::expected<application::FontCollection, application::FontError>
    collect(const std::vector<std::byte> &script, const application::FontEnvironment &environment,
            const std::atomic<bool> *cancel = nullptr) override;
    std::expected<application::ReimportCheck, application::FontError>
    verifyReimport(const std::vector<std::byte> &script, const application::FontCollection &collection,
                   const std::string &defaultFamily = {}) override;
    std::vector<application::SystemFace> pickerFaces(const application::FontEnvironment &environment) override;
    std::vector<bool> facesCover(const std::vector<application::SystemFace> &faces,
                                 const application::FontEnvironment &environment,
                                 const std::u32string &characters) override;
    void refresh() override;
    std::vector<std::string> fontDirectories() override;

private:
    std::atomic<bool> m_checkForUpdates{false}; // DirectWrite: rescan the system collection
};

} // namespace hikari::backends
