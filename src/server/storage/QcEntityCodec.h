#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "server/storage/QcEntityRecord.h"

#include <QByteArray>
#include <QSet>
#include <QStringList>

// The entities of one chunk as a blob of its region file (entities/r.<x>.<z>.qen; decisions E-366,
// E-373, E-420): persistent creatures, dropped items and waiting drops, falling blocks, armour stands,
// minecarts (E-382), experience orbs (E-398), projectiles (E-425), and whether the chunk already got its animals. Items,
// blocks, creatures, armour and capes are stored by name.
//
// Every entry is a header (type name, position, velocity, yaw, order, persistent id) and a list of
// components (QcComponentFrames): the fields of its kind and, for a villager, its record. A new field
// is a new version of its component or a new component, not a new world format.
namespace QcEntityCodec {

// Entries of one kind in one chunk at most; a valid world never comes close.
inline constexpr quint32 maxPerChunk = 65536;

// The ids of the components of entity entries; a later version adds ids below
// QcComponentFrames::firstModId, a mod from it on; none is ever reused.
enum class Component : quint16 {
    // Health, breeding timers, bred (version 1).
    Creature = 1,
    // QcVillagerRecord (QcVillagerCodec::version).
    Villager = 2,
    // Count, damage, age, pickup delay of a dropped stack (version 1).
    Item = 3,
    // Age of a falling block (version 1).
    FallingBlock = 4,
    // Pose, name, item, held item, armour, cape of an armour stand (version 1).
    ArmourStand = 5,
    // Item, slots and whether a hopper cart pulls (version 1).
    Minecart = 6,
    // Value, age, pickup delay of an experience orb (version 1).
    ExperienceOrb = 7,
    // Pitch, age, pickup, damage factor and where it sticks of a projectile (version 1).
    Projectile = 8,
    // The block state a creature carries, by name, and whether it is kept with the world only for
    // that block (version 1; decision E-460). 9 is the effects of package 52.2.
    Carried = 10,
    // A creature that stays in its enclosure (version 1, no fields; decision E-460).
    Confined = 11,
    // Whether a player built the golem (version 1, decision E-471).
    Golem = 12,
};

QByteArray encode(const QcEntityRecord &record, const QcContentRegistry &content);
// The record of `chunk`. Unknown items, blocks, creatures and capes and armour that no longer fits
// its slot are dropped with a warning (each unknown name once per world, through `reported`); damaged
// or implausible data (an entity outside the chunk, values that cannot be) is an error.
[[nodiscard]] QcResult<QcEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content, QStringList &warnings,
                                QSet<QString> &reported);

} // namespace QcEntityCodec
