#include "app/rendering/QcBufferWrite.h"

// Other platforms upload through QRhi's resource update batches.
namespace QcBufferWrite {

bool isSupported(const QRhi *)
{
    return false;
}

bool write(QRhiBuffer *, const void *, quint32)
{
    return false;
}

} // namespace QcBufferWrite
