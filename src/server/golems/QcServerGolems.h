// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcCreatureKindId.h"

#include <QtGlobal>

#include <array>
#include <optional>
#include <span>

class QcBlockAccess;
class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcItemDefinition;
struct QcMobDefinition;
struct QcServerClient;

/*! \brief Players and golems (decision E-471): building a golem from blocks and repairing one.

    A golem kind with \c {build} (QcGolemDefinition) comes alive when a player places its head block
    on the middle of four body blocks standing upright in a T, the T along either horizontal axis,
    with air beside the head and beside the foot (an iron golem: four iron blocks and a carved
    pumpkin in any of the four directions); the blocks vanish and a golem that never attacks players
    stands in their place. A golem kind with a \c {repairItem} is healed by \c {repairHealth} when a
    player uses that item on it (one of the stack, except in creative mode). */
class QcServerGolems
{
public:
    /*! \brief The cells of a built golem: its head, the middle and foot of its body and its two
        arms. */
    struct Shape
    {
        /*! Its head. */
        QcBlockPos head;
        /*! The foot of its body. */
        QcBlockPos foot;
        /*! The cells of its body and arms. */
        std::array<QcBlockPos, 5> cells{};
    };

    QcServerGolems(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory);

    /*! A player placed a block at \a pos: a golem whose head it is, standing on a complete body,
        comes alive. Returns the new golem's entity id, if one came. */
    std::optional<quint32> build(QcBlockPos pos);
    /*! The player of \a client uses the item \a held on the creature \a entityId: a golem it
        repairs is healed. Returns false when it is no repair (other uses may follow). */
    bool repair(QcServerClient &client, quint32 entityId, const QcItemDefinition *held);

    /*! Returns the T of \a definition's body blocks in \a world under its head at \a head, if
        complete (the head itself is not checked). */
    static std::optional<Shape> shapeAt(const QcBlockAccess &world, QcBlockPos head, const QcMobDefinition &definition);
    /*! The blocks \a cells of a creature built from blocks (a golem, the charred hydra) give it
        their life in the world of \a state: \a blocks removes them without drops, and a
        persistent creature of \a type stands in the middle of \a foot. Returns its entity id. */
    static quint32 wake(QcServerState &state, QcServerBlocks &blocks, QcCreatureKindId type, std::span<const QcBlockPos> cells, QcBlockPos foot);

private:
    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcServerInventory &m_inventory;
};
