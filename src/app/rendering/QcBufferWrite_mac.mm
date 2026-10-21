#include "app/rendering/QcBufferWrite.h"

#include <rhi/qrhi.h>

#import <Metal/Metal.h>

#include <cstring>

namespace QcBufferWrite {

bool isSupported(const QRhi *rhi)
{
    // The storage usage needs compute support; with it the Metal backend keeps one MTLBuffer per
    // buffer instead of one per frame in flight.
    return rhi && rhi->backend() == QRhi::Metal && rhi->isFeatureSupported(QRhi::Compute);
}

bool write(QRhiBuffer *buffer, const void *data, quint32 size)
{
    const QRhiBuffer::NativeBuffer native = buffer->nativeBuffer();
    if (native.slotCount != 1 || !native.objects[0] || size > buffer->size()) {
        return false;
    }

    // objects[0] points at the backend's id<MTLBuffer>.
    id<MTLBuffer> metal = (__bridge id<MTLBuffer>)(*static_cast<void *const *>(native.objects[0]));
    void *contents = metal ? [metal contents] : nullptr;
    if (!contents) {
        return false; // private storage: only a blit could fill it
    }

    std::memcpy(contents, data, size);
    // Managed storage (Macs with a separate GPU) keeps a GPU copy that has to be told.
    if (metal.storageMode == MTLStorageModeManaged) {
        [metal didModifyRange:NSMakeRange(0, size)];
    }
    return true;
}

} // namespace QcBufferWrite
