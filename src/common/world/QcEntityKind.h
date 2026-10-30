// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1StringView>
#include <QtGlobal>

#include <optional>

/*! \brief What an entity is (decision E-364): the server keeps every entity in the table of its
    kind, and the protocol names the kind of each entity it sends.

    The values are part of the protocol. */
enum class QcEntityKind : quint8 {
    /*! A player. */
    Player,
    /*! A creature of the content's mobs (EntitySpawn::creatureType names which). */
    Creature,
    /*! A dropped item stack. */
    Item,
    /*! A block falling while nothing holds it (sand, gravel). */
    FallingBlock,
    /*! An armour stand (decision E-238). */
    ArmourStand,
    /*! A block sliding where a piston moves it (decision E-337), shown only. */
    MovingBlock,
    /*! A cart that rolls along rails and carries a player (decision E-382). */
    Minecart,
    /*! A ball of experience that flies to the nearest player (decision E-398). */
    ExperienceOrb,
    /*! A shot in flight or stuck in a block (an arrow, decision E-425). */
    Projectile,
    /*! A lit explosive counting down to its explosion (decision E-601), drawn as its block. */
    PrimedExplosive,
    /*! An item frame or a painting on a block face (decision E-790). */
    Hanging,
    /*! A boat that floats on water and carries a player and a creature (decision E-920). */
    Boat,
};

/*! \brief The number of the entity kinds, their protocol values and names. */
namespace QcEntityKinds {

/*! The number of kinds. */
inline constexpr int count = 12;

/*! Returns the kind with the protocol value \a value; nothing for values no kind has. */
[[nodiscard]] std::optional<QcEntityKind> fromValue(quint8 value);
/*! Returns a short English name of \a kind for logs and dumps ("player", "creature", ...). */
QLatin1StringView name(QcEntityKind kind);

} // namespace QcEntityKinds
