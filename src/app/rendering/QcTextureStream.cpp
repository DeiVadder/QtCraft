#include "app/rendering/QcTextureStream.h"

#include "app/rendering/QcAtlasTexture.h"

#include <algorithm>
#include <utility>

bool QcTextureStream::start(QRhi *rhi, const QList<QImage> &levels)
{
    QList<QImage> levelsToUpload = QcAtlasTexture::chain(levels, rhi->resourceLimit(QRhi::TextureSizeMax));
    levelsToUpload.resize(std::min<qsizetype>(levelsToUpload.size(), rhi->mipLevelsForSize(levelsToUpload.front().size())));
    std::unique_ptr<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, levelsToUpload.front().size(), 1, QRhiTexture::MipMapped));
    if (!texture->create()) {
        return false;
    }
    m_texture = std::move(texture);
    setChain(std::move(levelsToUpload));
    setNext({});
    return true;
}

qint64 QcTextureStream::advance(QRhiResourceUpdateBatch *updates, qint64 budget)
{
    qint64 taken = 0;
    while (!done() && taken < budget) {
        const Position at = next();
        const QImage &level = chain()[at.level];
        const qint64 rowBytes = level.bytesPerLine();
        const int rows = std::clamp(static_cast<int>((budget - taken) / rowBytes), 1, level.height() - at.row);
        QRhiTextureSubresourceUploadDescription band(level);
        band.setSourceTopLeft({0, at.row});
        band.setSourceSize({level.width(), rows});
        band.setDestinationTopLeft({0, at.row});
        updates->uploadTexture(texture(), QRhiTextureUploadDescription({0, static_cast<int>(at.level), band}));
        taken += rowBytes * rows;
        setNext(at.row + rows < level.height() ? Position{at.level, at.row + rows} : Position{at.level + 1, 0});
    }
    return taken;
}

qint64 QcTextureStream::bytes() const
{
    qint64 total = 0;
    for (const QImage &level : chain()) {
        total += level.sizeInBytes();
    }
    return total;
}

std::unique_ptr<QRhiTexture> QcTextureStream::take()
{
    setChain({});
    setNext({});
    return std::exchange(m_texture, nullptr);
}
