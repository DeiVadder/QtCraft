#pragma once

#include "common/content/definitions/QcAppearance.h"
#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcHanging.h"
#include "common/gameplay/QcItemStack.h"
#include "common/gameplay/QcStandPose.h"
#include "common/world/QcEntityKind.h"

#include <QString>

// Client mirror of an entity the server reported (QcMsg::EntitySpawn).
struct QcClientEntity
{
    quint32 id = 0;
    QcEntityKind kind = QcEntityKind::Player;
    // Creatures: index into the content's mobs (checked on arrival).
    int creatureType = 0;
    // The block of a falling block; air otherwise.
    QcBlockId block = qcAirBlockId;
    // The stack of a dropped item, the item of a minecart (what it carries), a boat (what it is built
    // of) or a projectile (its look); empty otherwise.
    QcItemStack item;
    QString name;
    // Where the entity is shown at the end of the current client tick and at its start; frames
    // interpolate between them (decision E-271).
    QcVec3 position;
    QcVec3 previousPosition;
    float yaw = 0.0f;
    float previousYaw = 0.0f;
    // Players and creatures: their health; experience orbs: their value (decision E-398); lit
    // explosives: the ticks left of their fuse.
    double health = 0.0;
    // Client ticks left for the hurt flash.
    int hurtTicks = 0;
    // Players: their look; all creatures: head pitch, entity flag bits (QcMsg::Swing …) and the held item.
    QcAppearance appearance;
    float pitch = 0.0f;
    float previousPitch = 0.0f;
    quint16 flags = 0;
    QcItemId held = qcNoItem;
    // Players and armour stands: their armour slots.
    QcEquipment equipment;
    // Armour stands: pose and what they show.
    QcStandPose stand;
    // Item frames and paintings (decision E-790): where they hang and what they show; `held` is
    // their own item, `item` the stack a frame holds.
    QcHanging hanging;
    // Distance walked (drives the stride), the horizontal step of the latest client tick and the
    // walking speed in m/s smoothed over a few ticks; all from the shown (interpolated) motion.
    double walkDistance = 0.0;
    double lastStep = 0.0;
    double speed = 0.0;
    // Distance walked since the entity's last step sound.
    double sinceStep = 0.0;
    // Client ticks since the arm swing or the death started; negative when not running.
    int swingTicks = -1;
    int deathTicks = -1;

    bool isItem() const { return kind == QcEntityKind::Item; }
    bool isPlayer() const { return kind == QcEntityKind::Player; }
    bool isCreature() const { return kind == QcEntityKind::Creature; }
    bool isFallingBlock() const { return kind == QcEntityKind::FallingBlock; }
    bool isMovingBlock() const { return kind == QcEntityKind::MovingBlock; }
    // A lit explosive (decision E-601): `health` holds the ticks left of its fuse.
    bool isPrimedExplosive() const { return kind == QcEntityKind::PrimedExplosive; }
    // Falling and moving blocks and lit explosives: drawn as a block, never targeted, no steps.
    bool isBlockEntity() const { return isFallingBlock() || isMovingBlock() || isPrimedExplosive(); }
    bool isArmourStand() const { return kind == QcEntityKind::ArmourStand; }
    bool isMinecart() const { return kind == QcEntityKind::Minecart; }
    // A boat (decision E-920): `item` is its boat item, which names what it is built of.
    bool isBoat() const { return kind == QcEntityKind::Boat; }
    bool isExperienceOrb() const { return kind == QcEntityKind::ExperienceOrb; }
    bool isProjectile() const { return kind == QcEntityKind::Projectile; }
    // An item frame or a painting: targeted by its thin box (QcHanging::box), never moves.
    bool isHanging() const { return kind == QcEntityKind::Hanging; }
    // The block a minecart carries (its item's cartBlock, decision E-386); nothing for a plain cart.
    std::optional<QcBlockId> cartBlock(const QcContentRegistry &content) const
    {
        const bool known = isMinecart() && content.isValidItem(item.item);
        return known && !content.item(item.item).cartBlock.isEmpty() ? content.blockId(content.item(item.item).cartBlock) : std::nullopt;
    }
    // The definition of a creature; nothing for the other kinds.
    const QcMobDefinition *creature(const QcContentRegistry &content) const
    {
        return isCreature() && creatureType >= 0 && creatureType < content.mobs().size() ? &content.mobs()[creatureType] : nullptr;
    }
};
