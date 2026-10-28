#include "app/rendering/QcMaterialTextures.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcAtlasTexture.h"
#include "clientcore/materials/QcMaterialAtlas.h"
#include "clientcore/materials/QcMaterialParams.h"
#include "clientcore/materials/QcMaterialSynthesizer.h"

#include <utility>

namespace {

std::unique_ptr<QRhiTexture> flatTexture(QRhi *rhi, QColor texel, QRhiResourceUpdateBatch *updates)
{
    std::unique_ptr<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!texture->create()) {
        return nullptr;
    }
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(texel);
    updates->uploadTexture(texture.get(), image);
    return texture;
}

// A mipmapped RGBA8 texture of `levels` (and the smaller levels down to 1 x 1), null when refused.
std::unique_ptr<QRhiTexture> mapTexture(QRhi *rhi, const QList<QImage> &levels, QRhiResourceUpdateBatch *updates)
{
    const int maxSize = rhi->resourceLimit(QRhi::TextureSizeMax);
    const QList<QImage> chain = QcAtlasTexture::chain(levels, maxSize);
    std::unique_ptr<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, chain.front().size(), 1, QRhiTexture::MipMapped));
    if (!texture->create()) {
        return nullptr;
    }
    QList<QRhiTextureUploadEntry> entries;
    for (qsizetype level = 0; level < chain.size() && level < rhi->mipLevelsForSize(chain.front().size()); ++level) {
        entries.append({0, static_cast<int>(level), QRhiTextureSubresourceUploadDescription(chain[level])});
    }
    QRhiTextureUploadDescription description;
    description.setEntries(entries.cbegin(), entries.cend());
    updates->uploadTexture(texture.get(), description);
    return texture;
}

qint64 bytesOf(const QList<QImage> &levels)
{
    qint64 total = 0;
    for (const QImage &level : levels) {
        total += level.sizeInBytes();
    }
    return total;
}

} // namespace

const QcMaterialAtlas *QcMaterialTextures::target(QRhi *rhi, const std::shared_ptr<const QcMaterialAtlas> &maps, bool wanted) const
{
    if (!wanted || !maps || maps->normalLevels().isEmpty() || refused() == Request{rhi, maps->serial()}) {
        return nullptr;
    }
    return maps.get();
}

bool QcMaterialTextures::replaces(QRhi *rhi, const std::shared_ptr<const QcMaterialAtlas> &maps, bool wanted) const
{
    return holdsResources() && (m_gpu.rhi != rhi || m_gpu.maps.get() != target(rhi, maps, wanted));
}

bool QcMaterialTextures::prepare(QRhi *rhi, const std::shared_ptr<const QcMaterialAtlas> &maps, bool wanted, QRhiResourceUpdateBatch *updates)
{
    if (m_gpu.rhi != rhi) {
        release();
    }
    if (!usable(rhi)) {
        return false;
    }
    if (!m_gpu.flatNormal && !createFlat(rhi, updates)) {
        setRefused(Request{rhi, 0});
        return false;
    }

    const QcMaterialAtlas *const wantedMaps = target(rhi, maps, wanted);
    if (m_gpu.maps.get() == wantedMaps) {
        return true;
    }
    dropMaps();
    if (wantedMaps && !upload(maps, updates)) {
        qCWarning(lcRenderer) << "Material maps: the GPU refused the maps of" << maps->normalLevels().front().size()
                              << "; the realistic mode draws flat surfaces";
        setRefused(Request{rhi, maps->serial()});
    }
    return true;
}

bool QcMaterialTextures::createFlat(QRhi *rhi, QRhiResourceUpdateBatch *updates)
{
    // Uploaded in a batch of their own that joins `updates` only when everything was made.
    QRhiResourceUpdateBatch *const own = rhi->nextResourceUpdateBatch();
    Resources made;
    made.rhi = rhi;
    made.sampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::Linear, QRhiSampler::ClampToEdge,
                                       QRhiSampler::ClampToEdge));
    // The specular map's codes (metals 230..237 beside dielectric F0) must not blend between mip
    // levels either: its levels are picked, not mixed (decision E-908).
    made.specularSampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::ClampToEdge,
                                               QRhiSampler::ClampToEdge));
    const bool samplerMade = made.sampler->create() && made.specularSampler->create();
    made.flatNormal = samplerMade ? flatTexture(rhi, QColor::fromRgba(QcMaterialSynthesizer::flatNormal), own) : nullptr;
    made.flatSpecular = made.flatNormal ? flatTexture(rhi, QcMaterialSynthesizer::specularTexel(QcMaterialParams::flat(), 0.0f), own) : nullptr;
    if (!made.flatSpecular) {
        own->release();
        qCWarning(lcRenderer) << "Material maps: the GPU refused the flat textures";
        return false;
    }
    updates->merge(own);
    own->release();
    m_gpu = std::move(made);
    return true;
}

bool QcMaterialTextures::upload(const std::shared_ptr<const QcMaterialAtlas> &maps, QRhiResourceUpdateBatch *updates)
{
    QRhiResourceUpdateBatch *const own = m_gpu.rhi->nextResourceUpdateBatch();
    std::unique_ptr<QRhiTexture> normal = mapTexture(m_gpu.rhi, maps->normalLevels(), own);
    std::unique_ptr<QRhiTexture> specular = normal ? mapTexture(m_gpu.rhi, maps->specularLevels(), own) : nullptr;
    if (!specular) {
        own->release();
        return false;
    }
    updates->merge(own);
    own->release();
    m_gpu.normal = std::move(normal);
    m_gpu.specular = std::move(specular);
    m_gpu.maps = maps;
    m_gpu.bytes = bytesOf(maps->normalLevels()) + bytesOf(maps->specularLevels());
    return true;
}

bool QcMaterialTextures::usable(QRhi *rhi) const
{
    return refused() != Request{rhi, 0};
}

QList<QRhiShaderResourceBinding> QcMaterialTextures::entries() const
{
    if (!m_gpu.flatNormal) {
        return {};
    }
    constexpr auto fragment = QRhiShaderResourceBinding::FragmentStage;
    QRhiTexture *const normal = m_gpu.normal ? m_gpu.normal.get() : m_gpu.flatNormal.get();
    QRhiTexture *const specular = m_gpu.specular ? m_gpu.specular.get() : m_gpu.flatSpecular.get();
    return {QRhiShaderResourceBinding::sampledTexture(normalBinding, fragment, normal, m_gpu.sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(specularBinding, fragment, specular, m_gpu.specularSampler.get())};
}

void QcMaterialTextures::dropMaps()
{
    m_gpu.specular.reset();
    m_gpu.normal.reset();
    m_gpu.maps.reset();
    m_gpu.bytes = 0;
}

void QcMaterialTextures::release()
{
    // Handed out whole, so its destructor releases the members in reverse order.
    const Resources retired = std::exchange(m_gpu, {});
}

void QcMaterialTextures::reset()
{
    release();
    setRefused(std::nullopt);
}
