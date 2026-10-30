// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <optional>

/*! \brief Rules that derive block states from the surroundings, shared by the server and the
    client's block prediction (decision E-853): the state a placed block takes from the player's
    view and the clicked face, connections of fences, walls and panes, and whether a block still has
    the support it needs.

    Behaviour follows from data: the block's properties (facing, half, type, attach, hinge, open,
    north/east/south/west, up) and its definition flags. */
namespace QcBlockRules {

/*! \brief Where a block is placed and in which state. */
struct Placement
{
    /*! The block's position. */
    QcBlockPos pos;
    /*! Its state. */
    QcBlockId state = qcAirBlockId;
};

/*! \brief What the player aims at when placing a block: \c {face} of the block at \c {clicked},
    \c {hit} (0..1 inside that block), while looking along \c {yaw} and \c {pitch} (degrees,
    positive is up). \qcfields */
struct Aim
{
    QcBlockPos clicked;
    QcBlockFace face = QcBlockFace::Up;
    QcVec3 hit;
    float yaw = 0.0F;
    float pitch = 0.0F;
};

/*! Looking further up or down than this, a block whose \c {facing} allows up and down (pistons)
    faces vertically (decision E-334). */
inline constexpr float verticalFacingPitch = 45.0F;

/*! Returns the horizontal direction ("north", "east", "south", "west") a player with \a yaw
    (degrees) looks. */
QString lookDirection(float yaw);
/*! Returns the direction name of \a face ("up" and "down" for the vertical ones). */
QString faceName(QcBlockFace face);
/*! Returns the face opposite \a face. */
QcBlockFace opposite(QcBlockFace face);
/*! Returns the face the direction name \a name ("north" … "down", "floor", "ceiling") stands
    for. */
[[nodiscard]] std::optional<QcBlockFace> faceFromName(const QString &name);
/*! Returns the direction to the right of someone looking towards \a face (clockwise seen from
    above). */
[[nodiscard]] QcBlockFace rightOf(QcBlockFace face);

/*! Returns where and in which state \a block (any state of it) is placed in \a world when the
    player clicks with \a aim. A slab clicked on its open half becomes a double slab in place; a
    clicked replaceable block (tall grass) is replaced as if the new block were placed on the
    ground below it. std::nullopt when the block cannot go there (a torch on a ceiling, a door
    without room). */
[[nodiscard]] std::optional<Placement> placement(const QcBlockAccess &world, QcBlockId block, const Aim &aim);

/*! Returns \a id at \a pos with its connection properties recomputed from the neighbours in
    \a world (unchanged for blocks that do not connect); rails take their track from the rails
    around them (QcRails::shapedState), stairs their corner from the stairs around them
    (QcStairShapes::shapedState). Sides towards unloaded chunks keep their current value. */
QcBlockId connectedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

/*! Returns whether the block at \a pos (in state \a id) still has its support in \a world. */
bool isSupported(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

/*! Returns the block \a id at \a pos in \a world turns into when it touches its \c {hardensWith}
    block on any side (concrete powder next to water), std::nullopt otherwise. */
[[nodiscard]] std::optional<QcBlockId> hardened(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

/*! Returns the block \a id at \a pos in \a world turns into while an opaque full block lies on
    it (its \c {coveredInto}: a dirt path becoming dirt, decision E-406), std::nullopt otherwise. */
[[nodiscard]] std::optional<QcBlockId> covered(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

/*! Returns whether the other half of the door or bed \a id at \a pos is there in \a world (or not
    loaded). */
bool hasPartner(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

/*! Returns the other half of a door (above or below) or a bed (the head lies in the foot's
    \c {facing} direction) in state \a id at \a pos in \a world. */
QcBlockPos otherHalf(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);

} // namespace QcBlockRules
