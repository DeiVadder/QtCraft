// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/bastion/QcBastionGenerator.h"
#include "worldgen/fortress/QcFortressGenerator.h"
#include "worldgen/generators/QcWorldGenerator.h"
#include "worldgen/nether/QcNetherComplexes.h"
#include "worldgen/nether/QcNetherDecorator.h"
#include "worldgen/nether/QcNetherDefinitions.h"
#include "worldgen/nether/QcNetherDeltas.h"
#include "worldgen/nether/QcNetherGrowth.h"
#include "worldgen/nether/QcNetherStructures.h"
#include "worldgen/nether/QcNetherTerrain.h"

#include <memory>
#include <mutex>

/*! \brief World generator of the nether (decisions E-1030 to E-1032): caves between a floor and a
    roof of bedrock from 3D noise, a sea of lava, the nether's own biome map, and everything from
    worldgen/nether.json and the biome and ore files with \c {"dimension": "nether"}.

    Its versions are those of QcGeneratorVersion::nether. Deterministic per seed and thread-safe. */
class QcNetherGenerator : public QcWorldGenerator
{
public:
    /*! Builds the generator of \a seed with the definitions of \a content; with \a station the
        arrival station stands with its middle there. */
    QcNetherGenerator(const std::shared_ptr<const QcContentRegistry> &content, qint64 seed, std::optional<QcBlockPos> station = std::nullopt);

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override;
    QcBlockPos spawnPoint() const override;
    QString biomeAt(int x, int z) const override;
    bool isNaturalGround(QcBlockId block) const override;
    [[nodiscard]] std::optional<QcBlockId> platformBlock() const override { return definitions().filler; }
    QcChunkFurnishing furnishingIn(QcChunkPos pos) const override;
    [[nodiscard]] bool hasResidentsIn(QcChunkPos pos) const override { return bastion().hasResidentsIn(pos); }
    QcStructureSites structureSitesNear(QcBlockPos from, int reach) const override;
    [[nodiscard]] std::optional<QcStructureSites> plannedStructureSitesNear(QcBlockPos from, int reach) const override;
    [[nodiscard]] QcLocateResult locate(QStringView kind, QcBlockPos from, int reach) const override;
    [[nodiscard]] std::optional<QcStructurePlacement> nearestStructure(const QString &name, QcBlockPos from, int reach) const override;
    QStringList locatableNames() const override;
    [[nodiscard]] std::optional<QString> structureAt(QcBlockPos at) const override;
    QList<QcWorldLabel> labelsIn(QcChunkPos pos) const override { return structures().labelsIn(pos) + fortress().labelsIn(pos) + bastion().labelsIn(pos); }
    QList<QcBlockPos> liquidStartsIn(const QcChunk &chunk) const override { return m_decorator.springsIn(chunk); }

    /*! Returns the rock and the lava, for tests. */
    const QcNetherTerrain &terrain() const { return m_terrain; }
    /*! Returns the decorator, for tests. */
    const QcNetherDecorator &decorator() const { return m_decorator; }
    /*! Returns the structures, for tests. */
    const QcNetherStructures &structures() const { return m_structures; }
    /*! Returns the fortresses, for tests. */
    const QcFortressGenerator &fortress() const { return m_fortress; }
    /*! Returns what stands in which region, for tests. */
    const QcNetherComplexes &complexes() const { return m_complexes; }
    /*! Returns the bastions, for tests. */
    const QcBastionGenerator &bastion() const { return m_bastion; }
    /*! Returns the resolved settings. */
    const QcNetherDefinitions &definitions() const { return m_definitions; }

private:
    [[nodiscard]] QcFortressGenerator &fortressPlans() { return m_fortress; }
    [[nodiscard]] QcBastionGenerator &bastionPlans() { return m_bastion; }
    // Stands what a demonstration world shows around `station` and keeps the natural ones away.
    void setDemonstrations(QcBlockPos station);

    std::shared_ptr<const QcContentRegistry> m_content;
    QcNetherDefinitions m_definitions;
    QcNetherTerrain m_terrain;
    QcNetherDecorator m_decorator;
    QcNetherDeltas m_deltas;
    QcNetherGrowth m_growth;
    QcNetherComplexes m_complexes;
    QcFortressGenerator m_fortress;
    QcBastionGenerator m_bastion;
    QcNetherStructures m_structures;
    // The spawn, searched at the first call.
    mutable std::once_flag m_spawnOnce;
    mutable QcBlockPos m_spawn;
};
