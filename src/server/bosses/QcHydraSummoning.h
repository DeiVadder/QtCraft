// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QtGlobal>

#include <array>
#include <optional>

class QcBlockAccess;
class QcServerBlocks;
class QcServerState;
struct QcHydraDefinition;

/*! \brief Summoning the charred hydra (decision E-1125; the role model's wither): a player places
    the last of three QcHydraDefinition::summonHead blocks on a T of QcHydraDefinition::summonBody
    blocks.

    The T stands upright along either horizontal axis: three body blocks in a row, a fourth under
    their middle with air beside it, and a head block on each of the three; the placed head may be
    any of them, in creative mode too. The seven blocks vanish without drops, and a persistent
    hydra stands where the foot of the T was: it charges (QcHydraState::chargeTicks) with a third
    of its health, and every player within summonReach blocks gets the advancement trigger
    QcAdvancementTrigger::EntitySummoned. */
class QcHydraSummoning
{
public:
    /*! How far from the hydra players count as having summoned it (the role model's 50). */
    static constexpr double summonReach = 50.0;

    /*! \brief A complete T: its foot (where the hydra stands), whether it runs along x (else
        along z), and its seven cells. \qcfields */
    struct Shape
    {
        QcBlockPos foot;
        bool alongX = true;
        std::array<QcBlockPos, 7> cells{};
    };

    QcHydraSummoning(QcServerState &state, QcServerBlocks &blocks);

    /*! A player placed a block at \a pos: a hydra whose T it completes comes alive. Returns the new
        hydra's entity id, if one came. */
    std::optional<quint32> summon(QcBlockPos pos);

    /*! Returns the T of \a hydra in \a world that the head block at \a placed completes, if any
        (the placed block itself is checked too). */
    [[nodiscard]] static std::optional<Shape> shapeAt(const QcBlockAccess &world, QcBlockPos placed, const QcHydraDefinition &hydra);

private:
    QcServerState &m_state;
    QcServerBlocks &m_blocks;
};
