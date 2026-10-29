// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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

bool write(QRhiBuffer *buffer, quint32 offset, const void *data, quint32 size)
{
    const QRhiBuffer::NativeBuffer native = buffer->nativeBuffer();
    if (native.slotCount != 1 || !native.objects[0] || static_cast<quint64>(offset) + size > buffer->size()) {
        return false;
    }

    // objects[0] points at the backend's id<MTLBuffer>. An Objective-C bridge cast has only the
    // C-style form, so -Wold-style-cast is silenced for this one line (E-865).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
    id<MTLBuffer> metal = (__bridge id<MTLBuffer>)(*static_cast<void *const *>(native.objects[0]));
#pragma clang diagnostic pop
    void *contents = metal ? [metal contents] : nullptr;
    if (!contents) {
        return false; // private storage: only a blit could fill it
    }

    std::memcpy(static_cast<char *>(contents) + offset, data, size);
    // Managed storage (Macs with a separate GPU) keeps a GPU copy that has to be told.
    if (metal.storageMode == MTLStorageModeManaged) {
        [metal didModifyRange:NSMakeRange(offset, size)];
    }
    return true;
}

} // namespace QcBufferWrite
