#pragma once

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
};

// The names libass matches a font's faces by: family, typographic family, full and PostScript
// names, of every face of a collection.
std::vector<std::string> fontNames(const std::vector<std::byte> &bytes);

}
