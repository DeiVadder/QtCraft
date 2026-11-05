// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcGameMode.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes QcGameModes (qc_common) known to QML as GameMode, for its Mode enum: the menus compare a
// world's game mode with GameMode.Mode.Creative and so on (decision E-688).
namespace QcGameModesForeign {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(QcGameModes)
QML_NAMED_ELEMENT(GameMode)
} // namespace QcGameModesForeign
