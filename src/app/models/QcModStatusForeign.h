// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worlds/QcModStatus.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes QcModStatus (qc_worlds) known to QML as ModStatus, for its State enum: the mods page
// compares with ModStatus.State.Failed (decision E-688). Registered as a namespace, so that QML
// (and qmllint) reads the gadget as a holder of enums, not as a value type.
namespace QcModStatusForeign {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(QcModStatus)
QML_NAMED_ELEMENT(ModStatus)
} // namespace QcModStatusForeign
