// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes QcTexturePack (qc_clientcore) known to QML as TexturePack, for its Origin enum: a pack's
// row compares with TexturePack.Origin.Mod (decision E-688). Registered as a namespace, as
// QcModStatusForeign.h explains.
namespace QcTexturePackForeign {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(QcTexturePack)
QML_NAMED_ELEMENT(TexturePack)
} // namespace QcTexturePackForeign
