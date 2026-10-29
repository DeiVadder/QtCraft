// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"
#include "worldgen/generators/QcChunkFurnishing.h"
#include "worldgen/generators/QcWorldSetup.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>

#include <optional>

// A SHA-256 over what a world generator made (test_generator_versions, decision E-922): blocks by
// state name (so new blocks elsewhere in the content, which shift the numeric ids, change nothing),
// furnishing, village plans and the one-time setup. It also notes which block names and how many
// cave cells it saw, for the test's coverage checks.
class QcChunkChecksum
{
public:
    // Cells of air at most this high count as caves.
    static constexpr int caveTop = 40;

    explicit QcChunkChecksum(const QcContentRegistry &content)
        : m_content(content)
    {
    }

    void addInts(std::initializer_list<qint64> values)
    {
        for (const qint64 value : values) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): raw bytes of a trivially copyable value for the hash
            hash().addData(QByteArrayView(reinterpret_cast<const char *>(&value), static_cast<qsizetype>(sizeof(value))));
        }
    }

    void addString(const QString &text)
    {
        hash().addData(text.toUtf8());
        addInts({text.size()});
    }

    void addBlockPos(QcBlockPos pos) { addInts({pos.x, pos.y, pos.z}); }

    // The blocks column by column as runs of one state name.
    void addBlocks(const QcChunk &chunk)
    {
        std::optional<QcBlockId> run;
        qint64 length = 0;
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    const QcBlockId block = chunk.block(x, y, z);
                    if (block == qcAirBlockId && y > 0 && y <= caveTop) {
                        ++m_caveCells;
                    }
                    if (run == block) {
                        ++length;
                        continue;
                    }
                    addRun(run, length);
                    run = block;
                    length = 1;
                }
            }
        }
        addRun(run, length);
    }

    void addFurnishing(const QcChunkFurnishing &furnishing)
    {
        addInts({furnishing.chests.size(), furnishing.spawners.size(), furnishing.carts.size()});
        for (const QcChunkFurnishing::Chest &chest : furnishing.chests) {
            addBlockPos(chest.at);
            addString(chest.lootTable);
            addInts({static_cast<qint64>(chest.seed)});
        }
        for (const QcChunkFurnishing::Spawner &spawner : furnishing.spawners) {
            addBlockPos(spawner.at);
            addString(spawner.mob);
        }
        for (const QcChunkFurnishing::Cart &cart : furnishing.carts) {
            addBlockPos(cart.at);
            addString(cart.item);
            addString(cart.lootTable);
            addInts({static_cast<qint64>(cart.seed)});
        }
    }

    // What the plan holds beyond its blocks: style, layout, bounds, pieces, beds, workstations and
    // residents.
    void addVillage(const QcVillagePlan &plan)
    {
        addString(plan.style);
        addString(plan.layout);
        addBlockPos(plan.centre);
        addInts({plan.minX, plan.minZ, plan.maxX, plan.maxZ, plan.pieces.size()});
        for (const QcStructurePlacement &piece : plan.pieces) {
            addString(piece.structure().name());
            addBlockPos(piece.origin());
            addInts({piece.turns()});
        }
        for (const QList<QcVillagePlan::Spot> *spots : {&plan.beds, &plan.workstations}) {
            addInts({spots->size()});
            for (const QcVillagePlan::Spot &spot : *spots) {
                addBlockPos(spot.at);
                addString(spot.kind);
            }
        }
        for (const QcVillagePlan::Mob &mob : plan.mobs) {
            addString(mob.mob);
            addInts({static_cast<qint64>(mob.at.x * 1024.0), static_cast<qint64>(mob.at.y * 1024.0), static_cast<qint64>(mob.at.z * 1024.0)});
        }
    }

    // The rules and what the world brings along when it is created.
    void addSetup(const QcWorldSetup &setup)
    {
        addInts({setup.timeOfDay ? static_cast<qint64>(*setup.timeOfDay * 1000.0) : -1, setup.daylightCycle, setup.mobSpawning, setup.randomTicks,
                 setup.fireSpread, setup.weatherCycle, setup.mobGriefing, setup.explosionBlockDamage});
        addInts({setup.mobs.size(), setup.stands.size(), setup.contents.size(), setup.updates.size(), setup.growing.size(), setup.minecarts.size(),
                 setup.boats.size(), setup.signs.size(), setup.hangings.size()});
    }

    QString result() const { return QString::fromLatin1(m_hash.result().toHex()); }
    // The state names of every block seen and the air cells at most caveTop high.
    QSet<QString> seenNames() const { return QSet<QString>(m_names.cbegin(), m_names.cend()); }
    qint64 caveCells() const { return m_caveCells; }

private:
    QCryptographicHash &hash() { return m_hash; }

    void addRun(std::optional<QcBlockId> block, qint64 length)
    {
        if (!block) {
            return;
        }
        if (!m_names.contains(*block)) {
            m_names.insert(*block, m_content.stateName(*block));
        }
        addString(m_names.value(*block));
        addInts({length});
    }

    const QcContentRegistry &m_content;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QHash<QcBlockId, QString> m_names; // NOLINT(qc-member-access): a cache of the hasher, read and filled in addRun
    qint64 m_caveCells = 0;
};
