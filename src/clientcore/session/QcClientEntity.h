// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/mobs/QcAppearance.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcCreatureKindId.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcHanging.h"
#include "common/gameplay/QcItemStack.h"
#include "common/gameplay/QcStandPose.h"
#include "common/world/QcEntityKind.h"

#include <QString>

#include <optional>

/*! \brief Client mirror of an entity the server reported (QcMsg::EntitySpawn). */
struct QcClientEntity
{
    /*! The entity's id on the server. */
    quint32 id = 0;
    /*! What kind of entity it is; the kind decides which of the fields below mean something. */
    QcEntityKind kind = QcEntityKind::Player;
    /*! Creatures: their kind among the content's mobs (checked on arrival). */
    QcCreatureKindId creatureType;
    /*! The block of a falling block; air otherwise. */
    QcBlockId block = qcAirBlockId;
    /*! The stack of a dropped item, the item of a minecart (what it carries), a boat (what it is
        built of) or a projectile (its look); empty otherwise. */
    QcItemStack item;
    /*! Players: their name; creatures: their name tag, if any. */
    QString name;
    /*! Where the entity is shown at the end of the current client tick and at its start; frames
        interpolate between them (decision E-271). \qcshared */
    QcVec3 position;
    QcVec3 previousPosition;
    /*! Where the entity faces at the end of the current client tick, in degrees. */
    float yaw = 0.0f;
    /*! Where the entity faced at the start of the current client tick. */
    float previousYaw = 0.0f;
    /*! Players and creatures: their health; experience orbs: their value (decision E-398); lit
        explosives: the ticks left of their fuse. */
    double health = 0.0;
    /*! Client ticks left for the hurt flash. */
    int hurtTicks = 0;
    /*! The server time of a hurt whose flash waits until the shown moment reaches it, so the
        flash starts with the knockback the buffered motion shows (decision E-971); none when
        nothing waits. */
    std::optional<double> hurtAtMs;
    /*! Players: their look; all creatures: head pitch, entity flag bits (QcMsg::Swing …) and the
        held item. */
    QcAppearance appearance;
    /*! Head pitch at the end of the current client tick, in degrees. */
    float pitch = 0.0f;
    /*! Head pitch at the start of the current client tick. */
    float previousPitch = 0.0f;
    /*! Entity flag bits (QcMsg::Swing …). */
    quint16 flags = 0;
    /*! The held item; \c {qcNoItem} for none. */
    QcItemId held = qcNoItem;
    /*! Players and armour stands: their armour slots. */
    QcEquipment equipment;
    /*! Armour stands: pose and what they show. */
    QcStandPose stand;
    /*! Item frames and paintings (decision E-790): where they hang and what they show; \c {held}
        is their own item, \c {item} the stack a frame holds. */
    QcHanging hanging;
    /*! Distance walked (drives the stride), the horizontal step of the latest client tick and
        the walking speed in m/s smoothed over a few ticks; all from the shown (interpolated)
        motion. \qcshared */
    double walkDistance = 0.0;
    double lastStep = 0.0;
    double speed = 0.0;
    /*! Distance walked since the entity's last step sound. */
    double sinceStep = 0.0;
    /*! Client ticks since the arm swing or the death started; negative when not running.
        \qcshared */
    int swingTicks = -1;
    int deathTicks = -1;

    /*! Returns whether the entity is a dropped item. */
    bool isItem() const { return kind == QcEntityKind::Item; }
    /*! Returns whether the entity is a player. */
    bool isPlayer() const { return kind == QcEntityKind::Player; }
    /*! Returns whether the entity is a creature (creature()). */
    bool isCreature() const { return kind == QcEntityKind::Creature; }
    /*! Returns whether the entity is a falling block. */
    bool isFallingBlock() const { return kind == QcEntityKind::FallingBlock; }
    /*! Returns whether the entity is a block moved by a piston. */
    bool isMovingBlock() const { return kind == QcEntityKind::MovingBlock; }
    /*! Returns whether the entity is a lit explosive (decision E-601): \c {health} holds the
        ticks left of its fuse. */
    bool isPrimedExplosive() const { return kind == QcEntityKind::PrimedExplosive; }
    /*! Returns whether the entity is a falling or moving block or a lit explosive: drawn as a
        block, never targeted, no steps. */
    bool isBlockEntity() const { return isFallingBlock() || isMovingBlock() || isPrimedExplosive(); }
    /*! Returns whether the entity is an armour stand. */
    bool isArmourStand() const { return kind == QcEntityKind::ArmourStand; }
    /*! Returns whether the entity is a minecart (cartBlock()). */
    bool isMinecart() const { return kind == QcEntityKind::Minecart; }
    /*! Returns whether the entity is a boat (decision E-920): \c {item} is its boat item, which
        names what it is built of. */
    bool isBoat() const { return kind == QcEntityKind::Boat; }
    /*! Returns whether the entity is an experience orb. */
    bool isExperienceOrb() const { return kind == QcEntityKind::ExperienceOrb; }
    /*! Returns whether the entity is a projectile (arrow, thrown item). */
    bool isProjectile() const { return kind == QcEntityKind::Projectile; }
    /*! Returns whether the entity is an item frame or a painting: targeted by its thin box
        (QcHanging::box), never moves. */
    bool isHanging() const { return kind == QcEntityKind::Hanging; }
    /*! Returns the block a minecart carries (its item's cartBlock in \a content, decision
        E-386); nothing for a plain cart. */
    std::optional<QcBlockId> cartBlock(const QcContentRegistry &content) const
    {
        const bool known = isMinecart() && content.isValidItem(item.item);
        return known && !content.item(item.item).cartBlock.isEmpty() ? content.blockId(content.item(item.item).cartBlock) : std::nullopt;
    }
    /*! Returns the definition of a creature in \a content; nothing for the other kinds. */
    const QcMobDefinition *creature(const QcContentRegistry &content) const
    {
        return isCreature() && content.mobs().contains(creatureType) ? &content.mobs()[creatureType] : nullptr;
    }
};
