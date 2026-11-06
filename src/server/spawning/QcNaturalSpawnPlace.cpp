// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawning/QcNaturalSpawner.h"

#include "server/core/QcServerWorld.h"

const QString &QcNaturalSpawner::SpawnPlace::biomeIn(const QcServerWorld &world)
{
    if (!biome) {
        biome = world.generator().biomeAt(x, z);
    }
    return *biome;
}

int QcNaturalSpawner::SpawnPlace::lightIn(const QcServerWorld &world)
{
    if (!light) {
        light = world.blockLight({x, floorY + 1, z});
    }
    return *light;
}

int QcNaturalSpawner::SpawnPlace::skyLightIn(const QcServerWorld &world)
{
    if (!skyLight) {
        skyLight = world.skyLight({x, floorY + 1, z});
    }
    return *skyLight;
}

bool QcNaturalSpawner::SpawnPlace::darkEnough(const QcServerWorld &world, Sky sky, int maxBlockLight, QRandomGenerator &random)
{
    if (!draws) {
        draws = QcSpawnLight::draw(random);
    }
    return QcSpawnLight::darkEnough({skyLightIn(world), lightIn(world), sky.darkening, maxBlockLight}, draws.value_or(QcSpawnLight::Draws{}));
}

bool QcNaturalSpawner::SpawnPlace::lightAllows(const QcServerWorld &world, const SpawnRule &rule)
{
    return rule.maxLight >= QcLightEngine::maxLevel || lightIn(world) <= rule.maxLight;
}
