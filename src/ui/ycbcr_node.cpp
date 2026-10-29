#include "ycbcr_node.h"

#include "hikari/application/ycbcr_frame.h"

#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>

#include <rhi/qrhi.h>

#include <array>
#include <cstring>

namespace hikari::ui {

bool ycbcrSupported(QRhi *rhi)
{
    return rhi && rhi->isTextureFormatSupported(QRhiTexture::R8) && rhi->isTextureFormatSupported(QRhiTexture::RG8);
}

namespace {

class PlaneTexture : public QSGTexture {
public:
    explicit PlaneTexture(QRhiTexture::Format format) : m_format(format) {}
    ~PlaneTexture() override
    {
        if (m_texture)
            m_texture->deleteLater();
    }
    qint64 comparisonKey() const override { return static_cast<qint64>(reinterpret_cast<quintptr>(this)); }
    QRhiTexture *rhiTexture() const override { return m_texture; }
    QSize textureSize() const override { return m_size; }
    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps() const override { return false; }

    void setPlane(const std::byte *bytes, int stride, QSize size)
    {
        m_bytes = bytes;
        m_stride = stride;
        m_size = size;
        m_pending = true;
    }
    bool commit(QRhi *rhi, QRhiResourceUpdateBatch *updates, std::size_t &uploaded)
    {
        if (!m_pending)
            return true;
        if (!m_texture || m_texture->pixelSize() != m_size) {
            if (m_texture)
                m_texture->deleteLater();
            m_texture = nullptr;
            const int largest = rhi->resourceLimit(QRhi::TextureSizeMax);
            if (m_size.width() > largest || m_size.height() > largest)
                return false;
            m_texture = rhi->newTexture(m_format, m_size);
            if (!m_texture->create()) {
                delete m_texture;
                m_texture = nullptr;
                return false;
            }
        }
        m_pending = false;
        const qsizetype bytes = static_cast<qsizetype>(m_stride) * m_size.height();
        QRhiTextureSubresourceUploadDescription plane(
            QByteArray::fromRawData(reinterpret_cast<const char *>(m_bytes), bytes));
        plane.setDataStride(static_cast<quint32>(m_stride));
        updates->uploadTexture(m_texture, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, plane)}));
        uploaded += static_cast<std::size_t>(bytes);
        return true;
    }
    // MUST KILL: commitTextureOperations is a no-op; YcbcrShader commits the planes through YcbcrMaterial::commit
    void commitTextureOperations(QRhi *, QRhiResourceUpdateBatch *) override {}

private:
    QRhiTexture::Format m_format;
    QRhiTexture *m_texture = nullptr;
    QSize m_size;
    const std::byte *m_bytes = nullptr;
    int m_stride = 0;
    bool m_pending = false;
};

// std140: qt_Matrix, qt_Opacity, then four vec4 (YcbcrShader).
constexpr int kMatrixOffset = 0, kOpacityOffset = 64, kParametersOffset = 80, kUniformBytes = 144;

}

class YcbcrMaterial : public QSGMaterial {
public:
    YcbcrMaterial(std::function<void(std::size_t)> uploaded, std::function<void()> failed)
        : uploaded(std::move(uploaded)), failed(std::move(failed))
    {
    }
    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override
    {
        return this == other ? 0 : (this < other ? -1 : 1);
    }

    void setFrame(std::shared_ptr<const application::IndexedFrame> next)
    {
        frame = std::move(next);
        const auto &f = *frame;
        const auto &p = *f.ycbcr;
        const QSize chroma(p.chromaWidth, p.chromaHeight);
        luma.setPlane(f.bgra.data() + p.offset[0], p.stride[0], QSize(f.width, f.height));
        if (p.interleaved) {
            pairs.setPlane(f.bgra.data() + p.offset[1], p.stride[1], chroma);
        } else {
            first.setPlane(f.bgra.data() + p.offset[1], p.stride[1], chroma);
            second.setPlane(f.bgra.data() + p.offset[2], p.stride[2], chroma);
        }
        const auto &m = p.matrix;
        parameters = {float(m.cy), float(m.rv), float(m.gu), float(m.gv),
                      float(m.bu), float(m.yOffset), float(1 << (m.shift - 1)), 1.0f / float(1 << m.shift),
                      float(f.width), float(f.height), float(p.chromaWidth), float(p.chromaHeight),
                      p.interleaved ? 1.0f : 0.0f, p.fullHeightChroma ? 1.0f : 0.0f, 0.0f, 0.0f};
        interleaved = p.interleaved;
        pending = true;
    }
    QSGTexture *texture(int binding)
    {
        if (binding == 1)
            return &luma;
        if (interleaved)
            return &pairs;
        return binding == 2 ? static_cast<QSGTexture *>(&first) : &second;
    }
    void commit(QRhi *rhi, QRhiResourceUpdateBatch *updates)
    {
        if (!pending)
            return;
        pending = false;
        std::size_t bytes = 0;
        bool ok = luma.commit(rhi, updates, bytes);
        if (interleaved)
            ok = pairs.commit(rhi, updates, bytes) && ok;
        else
            ok = first.commit(rhi, updates, bytes) && second.commit(rhi, updates, bytes) && ok;
        failure = !ok;
        if (!ok) {
            if (failed)
                failed();
        } else if (uploaded) {
            uploaded(bytes);
        }
    }

    std::function<void(std::size_t)> uploaded;
    std::function<void()> failed;
    std::shared_ptr<const application::IndexedFrame> frame;
    PlaneTexture luma{QRhiTexture::R8}, pairs{QRhiTexture::RG8}, first{QRhiTexture::R8}, second{QRhiTexture::R8};
    // MUST KILL: parameters is a positional float[16] that must match ycbcr.frag's four vec4 in order
    std::array<float, 16> parameters{};
    bool interleaved = false;
    bool pending = false;
    bool failure = false;
};

namespace {

class YcbcrShader : public QSGMaterialShader {
public:
    YcbcrShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/hikari/ui/shaders/ycbcr.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/hikari/ui/shaders/ycbcr.frag.qsb"));
    }
    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        QByteArray *buffer = state.uniformData();
        Q_ASSERT(buffer->size() >= kUniformBytes);
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buffer->data() + kMatrixOffset, m.constData(), 64);
        }
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buffer->data() + kOpacityOffset, &opacity, 4);
        }
        const auto *material = static_cast<YcbcrMaterial *>(newMaterial);
        std::memcpy(buffer->data() + kParametersOffset, material->parameters.data(), sizeof material->parameters);
        return true;
    }
    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture, QSGMaterial *newMaterial,
                            QSGMaterial *) override
    {
        auto *material = static_cast<YcbcrMaterial *>(newMaterial);
        material->commit(state.rhi(), state.resourceUpdateBatch());
        QSGTexture *t = material->texture(binding);
        t->setFiltering(QSGTexture::Nearest);
        t->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        t->setVerticalWrapMode(QSGTexture::ClampToEdge);
        *texture = t;
    }
};

}

QSGMaterialShader *YcbcrMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new YcbcrShader;
}

YcbcrNode::YcbcrNode(std::function<void(std::size_t)> uploaded, std::function<void()> failed)
    : m_material(new YcbcrMaterial(std::move(uploaded), std::move(failed)))
{
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    setGeometry(geometry);
    setFlag(OwnsGeometry);
    setMaterial(m_material);
    setFlag(OwnsMaterial);
}

YcbcrNode::~YcbcrNode() = default;

void YcbcrNode::setFrame(std::shared_ptr<const application::IndexedFrame> frame)
{
    m_material->setFrame(std::move(frame));
    markDirty(DirtyMaterial);
}

void YcbcrNode::place(const QRectF &target, const QRectF &source)
{
    const auto &f = *m_material->frame;
    const QSizeF size(f.width, f.height);
    if (target == m_target && source == m_source && size == m_size)
        return;
    m_target = target;
    m_source = source;
    m_size = size;
    const QRectF normalized(source.x() / f.width, source.y() / f.height, source.width() / f.width,
                            source.height() / f.height);
    QSGGeometry::updateTexturedRectGeometry(geometry(), target, normalized);
    markDirty(DirtyGeometry);
}

bool YcbcrNode::failed() const
{
    return m_material->failure;
}

}
