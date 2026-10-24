#include "app/rendering/QcAtlasTexture.h"

#include "app/debug/QcAppLogging.h"
#include "clientcore/textures/QcAtlasLayout.h"
#include "clientcore/textures/QcGpuLimits.h"

#include <QtGlobal>

namespace QcAtlasTexture {

QList<QImage> chain(const QList<QImage> &levels, int maxSize)
{
    QList<QImage> chain;
    const qsizetype first = QcAtlasLayout::firstLevelWithin(levels, maxSize);
    if (first >= 0) {
        for (qsizetype level = first; level < levels.size(); ++level) {
            chain.append(levels[level].convertToFormat(QImage::Format_RGBA8888));
        }
    } else {
        chain.append(levels.back().scaled(std::min(maxSize, levels.back().width()), std::min(maxSize, levels.back().height()))
                         .convertToFormat(QImage::Format_RGBA8888));
    }

    while (chain.back().width() > 1 || chain.back().height() > 1) {
        const QImage &last = chain.back();
        if (last.width() >= 2 && last.height() >= 2) {
            chain.append(QcAtlasLayout::halve(last));
        } else {
            chain.append(last.scaled(std::max(1, last.width() / 2), std::max(1, last.height() / 2), Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGBA8888));
        }
    }
    return chain;
}

[[nodiscard]] std::optional<qsizetype> usableLevel(const QList<QImage> &levels, int maxSize, const std::function<bool(QSize)> &creates)
{
    const qsizetype first = std::max<qsizetype>(0, QcAtlasLayout::firstLevelWithin(levels, maxSize));
    for (qsizetype level = first; level < levels.size(); ++level) {
        if (creates(levels[level].size())) {
            return level;
        }
    }
    return std::nullopt;
}

QImage placeholder()
{
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(Qt::magenta);
    return image;
}

namespace {

std::unique_ptr<QRhiTexture> newTexture(QRhi *rhi, QSize size)
{
    std::unique_ptr<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::MipMapped));
    return texture->create() ? std::move(texture) : nullptr;
}

void upload(QRhi *rhi, QRhiTexture *texture, const QList<QImage> &levels, QRhiResourceUpdateBatch *updates)
{
    QList<QRhiTextureUploadEntry> entries;
    for (qsizetype level = 0; level < levels.size() && level < rhi->mipLevelsForSize(levels.front().size()); ++level) {
        entries.append({0, static_cast<int>(level), QRhiTextureSubresourceUploadDescription(levels[level])});
    }
    QRhiTextureUploadDescription description;
    description.setEntries(entries.cbegin(), entries.cend());
    updates->uploadTexture(texture, description);
}

} // namespace

std::unique_ptr<QRhiTexture> create(QRhi *rhi, const QList<QImage> &levels, QRhiResourceUpdateBatch *updates)
{
    const int maxSize = rhi->resourceLimit(QRhi::TextureSizeMax);
    QcGpuLimits::report(maxSize);

    // A texture the GPU refuses (out of memory) is tried again one level smaller, then as the
    // placeholder; null only when even that fails.
    std::unique_ptr<QRhiTexture> texture;
    const std::optional<qsizetype> level = usableLevel(levels, maxSize, [rhi, &texture](QSize size) {
        texture = newTexture(rhi, size);
        return texture != nullptr;
    });
    if (level) {
        if (*level > std::max<qsizetype>(0, QcAtlasLayout::firstLevelWithin(levels, maxSize))) {
            qCWarning(lcRenderer, "Texture atlas: the GPU refused the larger levels, using mip level %d", int(*level));
        }
        upload(rhi, texture.get(), chain(levels.mid(*level), maxSize), updates);
        return texture;
    }

    qCWarning(lcRenderer, "Texture atlas: the GPU refused every level; textures are drawn with the placeholder");
    texture = newTexture(rhi, QSize(16, 16));
    if (texture) {
        upload(rhi, texture.get(), chain({placeholder()}, maxSize), updates);
    }
    return texture;
}

std::unique_ptr<QRhiTexture> createAnimationMap(QRhi *rhi, const QImage &map, QRhiResourceUpdateBatch *updates)
{
    QImage image = map.isNull() ? QImage() : map.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_RGBA8888);
        image.fill(QColor(0, 0, 0, 0));
    }

    std::unique_ptr<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, image.size()));
    if (!texture->create()) {
        qCWarning(lcRenderer) << "Texture atlas: the GPU refused the animation map of" << image.size();
        return nullptr;
    }
    updates->uploadTexture(texture.get(), image);
    return texture;
}

std::unique_ptr<QRhiSampler> createSampler(QRhi *rhi)
{
    std::unique_ptr<QRhiSampler> sampler(
        rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::Linear, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    sampler->create();
    return sampler;
}

std::unique_ptr<QRhiSampler> createAnimationMapSampler(QRhi *rhi)
{
    std::unique_ptr<QRhiSampler> sampler(
        rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    sampler->create();
    return sampler;
}

} // namespace QcAtlasTexture
