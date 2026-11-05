// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <memory>

namespace QcTestUtil {

// A server of the world `name` with `seed` in `directory` on the fast test terrain, its chunks kept
// loaded and its world open (the case fails when it does not open).
inline std::unique_ptr<QcServer> openTestTerrainWorld(const QString &directory, const std::shared_ptr<const QcContentRegistry> &content,
                                                      const QString &name, qint64 seed)
{
    QcServerConfig config;
    config.worldName = name;
    config.seed = seed;
    config.worldDirectory = directory;
    config.keepChunksLoaded = true;
    auto server = std::make_unique<QcServer>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
    if (const QcStatus opened = server->openWorld(); !opened) {
        failCase(QStringLiteral("The world in %1 does not open: %2").arg(directory, opened.error()));
    }
    return server;
}

} // namespace QcTestUtil
