// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "server/explosions/QcBlastBlocks.h"
#include "server/explosions/QcBlastBodies.h"
#include "server/explosions/QcBlastTable.h"

#include <optional>

class QcPlayerHealth;
class QcServerBlocks;
class QcServerFire;
class QcServerSounds;
class QcServerState;

// Explosives and explosions in the server tick (decisions E-600 to E-604).
// - An explosive block (QcBlastDefinition) is lit by flint and steel, a dispenser with it, fire
//   burning it, signal power (QcSignalKind::Igniter) or another explosion: it leaves its cell and
//   becomes a lit explosive (QcPrimedExplosive) that falls and counts its fuse down. Beyond the
//   limit `primedExplosives` nothing is lit and the block stays.
// - When the fuse runs out it explodes: at most `explosionsPerTick` per tick, the others wait with
//   their fuse at 0 in the order of the table. A blast hurts and pushes the players and creatures
//   that see it (QcBlastBodies) and, while the game rule explosionBlockDamage holds, breaks the cells
//   its rays reach (QcExplosionRays, at most `explosionBlocks` of them, the nearest first), which
//   QcBlastBlocks works through at most `explosionBlocksPerTick` per tick. Explosives among them are
//   lit with a short fuse of their own (chainFuseMin to chainFuseMax ticks, by dice), so a chain of
//   any size spreads over many ticks and no tick does more than its budgets allow.
// - Every explosion is sent to the clients near it (QcMsg::Explosion: flash and smoke) with its sound.
class QcServerExplosions
{
public:
    struct Numbers
    {
        quint64 lit = 0;
        // Explosives that were to be lit but the table was full.
        quint64 refused = 0;
        quint64 explosions = 0;
        // Explosions that had to wait a tick for a place among the explosions of a tick.
        quint64 waited = 0;
        quint64 chained = 0;
        quint64 playersHit = 0;
        quint64 creaturesHit = 0;
    };

    // An explosion of `power` at `centre`; with `breaksBlocks` (the game rule, and for a creature
    // also mobGriefing) it breaks blocks, with `fire` it lights fires among them.
    struct Blast
    {
        QcVec3 centre;
        double power = 4.0;
        bool breaksBlocks = true;
        bool fire = false;
    };

    // Clients hear and see an explosion this far away.
    static constexpr double viewDistance = 96.0;

    QcServerExplosions(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerFire &fire, QcPlayerHealth &health);

    // Lights the explosive block at `pos` with its own fuse (or `fuse` ticks); false when the cell
    // holds no explosive, its chunk is not loaded or the table of lit explosives is full.
    bool light(QcBlockPos pos, std::optional<int> fuse = std::nullopt);
    // Sets off a blast now (tests, commands and later creatures; lit explosives go through tick()).
    void explode(const Blast &blast);
    // Counts the fuses down, sets off the due explosives and works through the waiting cells.
    void tick();

    // Whether explosions break blocks now: the game rule explosionBlockDamage.
    bool breaksBlocks() const;
    bool isExplosive(QcBlockId block) const { return m_table.of(block).explosive; }
    const QcBlastTable &table() const { return m_table; }
    const Numbers &numbers() const { return m_numbers; }
    quint64 blocksBroken() const { return m_blocks.broken(); }
    qsizetype waitingCells() const { return m_blocks.waiting(); }

private:
    // Lights `block` that stood at `pos` (already checked) with `fuse` ticks.
    bool lightBlock(QcBlockPos pos, QcBlockId block, int fuse);
    int chainFuse(QcBlockPos pos, QcBlockId block) const;
    void announce(const Blast &blast);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlastTable m_table;
    QcBlastBlocks m_blocks;
    QcBlastBodies m_bodies;
    Numbers m_numbers;
};
