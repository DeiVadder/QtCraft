#pragma once

#include <QtGlobal>

class QRhi;
class QRhiBuffer;

// Fills new section buffers by writing straight into their memory instead of through a QRhi
// resource update batch (decision E-813). A batch copies the data of every upload into one of its
// operations and keeps that allocation for reuse when the batch returns to QRhi's pool, so the pool
// ends up holding about as much CPU memory as the largest uploads it has ever seen (with a world
// loading in, nearly all section data). Only for buffers that are written once before any draw
// reads them.
namespace QcBufferWrite {

// Whether buffers of this QRhi created with the storage usage can be written directly (Metal: one
// copy per buffer in memory the CPU can reach).
bool isSupported(const QRhi *rhi);
// Copies `size` bytes to the start of `buffer`, a buffer of a QRhi for which isSupported() holds,
// created with the storage usage and not yet drawn from; false when it cannot (the caller uploads
// through a batch instead).
[[nodiscard]] bool write(QRhiBuffer *buffer, const void *data, quint32 size);

} // namespace QcBufferWrite
