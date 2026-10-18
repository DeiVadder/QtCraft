#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockFace.h"
#include "server/entities/QcCreature.h"

#include <QHash>

#include <utility>

class QcServerBlocks;
class QcServerState;

// Infested blocks and the creatures that hide in them (silverfish, decision E-453). An infested
// block (QcBlockDefinition::infestsBlock, infestsMob) lets its creature out when it breaks with drops
// (QcServerBlocks::breakBlock calls release). A creature of a kind that hides in blocks
// (QcMobDefinition::hidesInBlocks) that was hurt wakes its kind in the infested blocks around it; an
// idle one now and then turns a block next to it into the infested block of its kind and is gone.
// A creature that may not change the world (QcCreature::mayGrief: the game rule mobGriefing off, as
// in the reference, or the creature confined or without a mind; decision E-510) does not hide, and
// its waking puts the base block back instead of breaking the infested one (no creature comes out).
class QcInfestation
{
public:
    // A hurt creature wakes its kind in infested blocks up to this far around it, sideways and up or
    // down (the reference's 10 and 5).
    static constexpr int wakeReach = 10;
    static constexpr int wakeHeight = 5;
    // Chance per tick that an idle creature that hides in blocks tries to hide.
    static constexpr double hideChance = 0.05;

    QcInfestation(QcServerState &state, QcServerBlocks &blocks);

    // The creature of the infested block `definition` comes out where it broke at `pos`; its id, 0
    // for none (no infested block, an unknown kind).
    static quint32 release(QcServerState &state, const QcBlockDefinition &definition, QcBlockPos pos);
    // Before the creatures move: those hurt since their last move wake their kind, idle ones hide.
    void tick();
    // Wakes the kind of `mob` in the infested blocks around it: they break one after another, nearest
    // layers first, and after each one the waking stops with even chance (as in the reference); with
    // a creature that may not change the world each turns back into its base block instead. How many
    // blocks woke.
    int wake(const QcCreature &mob);
    // Hides `mob` in the block next to the middle of its body towards `face` when that block has an
    // infested block of its kind and it may change the world: the block turns into it and the
    // creature is removed. Whether it hid.
    bool hide(const QcCreature &mob, QcBlockFace face);

private:
    // The infested blocks of the content, mapped once (it does not change while the server runs).
    struct Map
    {
        // The infested block of a base block for a creature kind: (base block, kind) -> infested block.
        QHash<std::pair<QcBlockId, int>, QcBlockId> hiding;
        // The infested blocks that let a creature kind out, with the base block each stands in for:
        // kind -> (infested block -> base block).
        QHash<int, QHash<QcBlockId, QcBlockId>> waking;
    };

    const Map &map();
    bool hides(const QcCreature &mob) const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    Map m_map;
    bool m_mapped = false;
};
