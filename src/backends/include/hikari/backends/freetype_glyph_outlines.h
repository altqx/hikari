#pragma once

#include "hikari/application/font_service.h"
#include "hikari/application/shape_kernel.h"

#include <functional>
#include <memory>

namespace hikari::backends {

// Text outlines in the fonts libass would pick (through `fonts`), read with FreeType: glyphs on their
// advances and kerning, synthetic bold and italic as libass applies them, underline and strike-out as
// rectangles.
class FreetypeGlyphOutlines final : public application::shape::GlyphOutlinePort {
public:
    using Environment = std::function<application::FontEnvironment()>;
    explicit FreetypeGlyphOutlines(application::FontServicePort &fonts, Environment environment = {});
    ~FreetypeGlyphOutlines() override;
    std::optional<application::shape::TextOutline> outline(const application::shape::GlyphStyle &style,
                                                           std::string_view utf8) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
