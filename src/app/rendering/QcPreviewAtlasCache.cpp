#include "QcPreviewAtlasCache.h"

#include "QcAtlasTexture.h"

#include <QMutex>

#include <algorithm>
#include <vector>

namespace QcPreviewAtlasCache {

namespace {

struct Entry
{
    QRhi *rhi = nullptr;
    // QImage::cacheKey() of the atlas image: copies of one image share it.
    qint64 imageKey = 0;
    std::weak_ptr<QRhiTexture> texture;
};

QMutex mutex;
std::vector<Entry> entries;

void dropExpired()
{
    std::erase_if(entries, [](const Entry &entry) { return entry.texture.expired(); });
}

} // namespace

std::shared_ptr<QRhiTexture> texture(QRhi *rhi, const QList<QImage> &levels, QRhiResourceUpdateBatch *updates)
{
    const QImage &image = levels.front();
    const QMutexLocker locker(&mutex);
    dropExpired();
    const auto known = std::ranges::find_if(entries, [rhi, &image](const Entry &entry) { return entry.rhi == rhi && entry.imageKey == image.cacheKey(); });
    if (known != entries.end()) {
        // Its last holder may be letting go right now; then it is uploaded anew.
        if (std::shared_ptr<QRhiTexture> shared = known->texture.lock()) {
            return shared;
        }
        entries.erase(known);
    }

    std::shared_ptr<QRhiTexture> created = QcAtlasTexture::create(rhi, levels, updates);
    if (!created) {
        return nullptr;
    }
    entries.push_back({rhi, image.cacheKey(), created});
    return created;
}

int liveTextures()
{
    const QMutexLocker locker(&mutex);
    dropExpired();
    return int(entries.size());
}

} // namespace QcPreviewAtlasCache
