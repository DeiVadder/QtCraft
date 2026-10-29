// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArrayView>

// CRC-32 (IEEE 802.3, as used by zip and PNG) for integrity checks of save files.
quint32 qcCrc32(QByteArrayView data);
