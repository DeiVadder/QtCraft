// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/hooks/QcMenuPreselect.h"

#include "app/controllers/QcNewWorldDraft.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "common/gameplay/QcGameMode.h"
#include "worldgen/generators/QcWorldGenerators.h"

namespace QcMenuPreselect {

void newWorld(QcNewWorldDraft &draft, const QString &worldType, const QString &gameMode)
{
    if (!worldType.isEmpty()) {
        const auto type = QcWorldLibraryController::worldTypeOf(worldType);
        if (type == QcWorldLibraryController::WorldType::Terrain && worldType != QLatin1StringView(QcWorldGenerators::terrain)) {
            qCWarning(lcStartup) << "Test hook: unknown world type" << worldType;
        } else {
            draft.setStartWorldType(type);
        }
    }
    if (!gameMode.isEmpty()) {
        const std::optional<QcGameMode> mode = QcGameModes::parse(gameMode);
        if (!mode) {
            qCWarning(lcStartup) << "Test hook: unknown game mode" << gameMode;
        } else {
            draft.setStartGameMode(*mode);
        }
    }
}

} // namespace QcMenuPreselect
