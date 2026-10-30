// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcMobDefinition.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Reads one mob definition of a content pack: owns the definition being built and the
    reader of the mob's JSON object.

    The parts run in a fixed order, later parts check fields that earlier parts read (the AI
    checks \c {hostile}, the traits check the AI). The traits of creatures that teleport, carry
    blocks, are tamed, swim or guard a zone are read in QcMobReaderTraits.cpp. */
class QcMobReader
{
public:
    QcMobReader(const QJsonObject &json, const QString &packNamespace);

    /*! Returns the mob definition read from the JSON object, or the first error found. */
    [[nodiscard]] QcResult<QcMobDefinition> read();

private:
    void parseMobStats();
    void parseSpawn();
    void parseSpawnConditions(const QJsonObject &spawn, QcJsonFieldReader &spawnReader);
    void parseSpawnLimits(const QJsonObject &spawn, QcJsonFieldReader &spawnReader);
    void parseExtraSpawns(const QJsonObject &spawn, QcJsonFieldReader &spawnReader);
    void parseKeeping();
    void parseCatch(QcJsonFieldReader &catchReader);
    void parseMobModel();
    void parseVillager();
    void parseAi();
    void parseFighting();
    void parseHostileTraits();
    void parseMobSounds();
    // "family" and "hover" (decision E-839).
    void parseFamily();
    void checkWaterSpawns();

    // QcMobReaderTraits.cpp: "gaze", "teleport", "carries", "waterDamage", companions, water
    // creatures, milking, crumbling, guarding and drinking.
    void parseCreatureTraits();
    void parseCompanions();
    void parseWaterAndMilk();
    void parseGuardian();

    const QJsonObject &m_json;
    const QString &m_namespace;
    QcJsonFieldReader m_reader;
    QcMobDefinition m_mob;
};
