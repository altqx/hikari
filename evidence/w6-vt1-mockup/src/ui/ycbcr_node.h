#pragma once

// V1: the presenter's frame layer for a player frame kept as Y'CbCr planes
// (IndexedFrame::ycbcr). The planes are uploaded as they are (R8 luma, R8 or
// RG8 chroma: about 1.5 bytes a pixel where BGRA takes 4) and converted in
// the fragment shader (shaders/ycbcr.frag) with the CPU conversion's
// fixed-point formula, so a texel is the byte the CPU path makes. Only on an
// RHI backend with R8 and RG8 textures (ycbcrSupported); the presenter
// converts on the CPU elsewhere (application::ycbcrToBgra).

#include "hikari/application/indexed_source.h"

#include <QRectF>
#include <QSGGeometryNode>

#include <functional>
#include <memory>

class QRhi;

namespace hikari::ui {

class YcbcrMaterial;

bool ycbcrSupported(QRhi *rhi);

class YcbcrNode : public QSGGeometryNode {
public:
    // `uploaded(bytes)` after a frame's planes are uploaded, `failed()` when
    // a plane's texture cannot be made (render thread).
    YcbcrNode(std::function<void(std::size_t)> uploaded, std::function<void()> failed);
    ~YcbcrNode() override;

    // The frame to upload at the next render; `frame` must hold planes.
    void setFrame(std::shared_ptr<const application::IndexedFrame> frame);
    // Where the frame is drawn, and the part of the frame (pixels) drawn there.
    void place(const QRectF &target, const QRectF &source);
    // The last upload could not make a texture.
    bool failed() const;

private:
    YcbcrMaterial *m_material;
    QRectF m_target, m_source;
    QSizeF m_size;
};

} // namespace hikari::ui
