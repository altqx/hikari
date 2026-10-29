#pragma once

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
    YcbcrNode(std::function<void(std::size_t)> uploaded, std::function<void()> failed);
    ~YcbcrNode() override;

    void setFrame(std::shared_ptr<const application::IndexedFrame> frame);
    void place(const QRectF &target, const QRectF &source);
    bool failed() const;

private:
    YcbcrMaterial *m_material;
    QRectF m_target, m_source;
    QSizeF m_size;
};

}
