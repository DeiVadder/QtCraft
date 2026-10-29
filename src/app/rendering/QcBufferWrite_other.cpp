// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcBufferWrite.h"

// Other platforms upload through QRhi's resource update batches.
namespace QcBufferWrite {

bool isSupported(const QRhi *)
{
    return false;
}

bool write(QRhiBuffer *, quint32, const void *, quint32)
{
    return false;
}

} // namespace QcBufferWrite
