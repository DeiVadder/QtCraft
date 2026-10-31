// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcChunkPos.h"

#include <QList>

#include <array>
#include <functional>
#include <memory>
#include <utility>

class QcChunk;
class QcMobManager;
class QcServerWorld;

/*! \brief The creatures of a chunk seen for the first time: its group of persistent animals
    (decision E-086) and the creatures of the village pieces in it (decision E-404); and the seed's
    sets of chunks of the kinds that spawn only in some (slime chunks).

    Part of QcNaturalSpawner; everything it draws comes from the seed and the chunk's position,
    so a chunk gets the same animals in every start of the world. */
class QcChunkSpawns
{
public:
    /*! Share of newly generated chunks that get a group of animals. */
    static constexpr double chunkAnimalChance = 0.1;
    /*! A group stands on these neighbouring columns. */
    static constexpr std::array<std::pair<int, int>, 4> groupOffsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};

    /*! Gives the entity id of the next creature. */
    using NextId = std::function<quint32()>;

    /*! Returns whether creatures of \a definition are animals: peaceful walkers that are kept
        with the world, not flying, ambient or water creatures, not villagers or golems (they
        belong to villages, decisions E-411, E-470). */
    static bool isAnimal(const QcMobDefinition &definition);

    /*! \a mobs owns the creatures this adds and outlives it; kinds from \a content, chances
        decided by the world \a seed. */
    QcChunkSpawns(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    /*! Spawns the persistent creatures of the village plan of \a world whose spot lies in the
        chunk at \a pos, with ids from \a nextEntityId. */
    void spawnVillageMobs(const QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId);
    /*! Spawns the group of animals of \a chunk at \a pos in \a world (chunkAnimalChance, decided by
        the seed) on the highest fitting ground of a random column, with ids from \a nextEntityId.
    */
    void spawnChunkAnimals(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, const NextId &nextEntityId);
    /*! Returns whether the seed lets the kind \a type with the chunk chance \a chance below 1 spawn
        in the chunk at \a pos; \a rule is the index of its spawn rule (0: main rule), each rule has
        its own set of chunks. */
    bool spawnsInChunk(int type, QcChunkPos pos, double chance, int rule) const;

private:
    // Peaceful kinds that may live on `floor` (chunk-local ground block) of a newly seen chunk.
    QList<int> animalCandidates(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, QcBlockPos floor) const;
    // Weighted choice by spawn weight for a `random` value in [0, 1).
    int pickAnimal(const QList<int> &candidates, double random) const;

    QcMobManager &m_mobs;
    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
};
