#pragma once

#include <QByteArrayView>

// CRC-32 (IEEE 802.3, as used by zip and PNG) for integrity checks of save files.
quint32 qcCrc32(QByteArrayView data);
