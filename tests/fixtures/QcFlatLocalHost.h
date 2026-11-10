// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <memory>

// A local server of an unsaved flat survival world (seed 1, view distance 2) for tests that drive a
// client session over a link; the caller checks startError().
inline std::unique_ptr<QcLocalServerHost> qcFlatSurvivalHost(const std::shared_ptr<const QcContentRegistry> &content)
{
    QcServerConfig config;
    config.viewDistance = 2;
    config.seed = 1;
    config.generator = QString::fromLatin1(QcWorldGenerators::flat);
    config.defaultGameMode = QcGameMode::Survival;
    return std::make_unique<QcLocalServerHost>(config, content, QcWorldGenerators::create(config.generator, content, 1, {}, QcGeneratorVersion::current));
}
