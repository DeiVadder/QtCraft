// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QHash>
#include <QString>

#include <optional>

class QcServerSounds;
class QcServerState;
struct QcServerClient;

// The open and close sounds of storage blocks (decision E-879): a chest, barrel or placed backpack
// sounds when its first viewer opens it and when its last one closes it, so that two players looking
// into one chest make one sound each way; a backpack opened from the hand sounds when its holder
// opens and closes it. It compares who looks into what with the previous tick, so every way a screen
// closes (the player, the server, death, walking away) sounds alike. The sounds are the block's
// QcBlockDefinition::containerOpenSound and containerCloseSound. A container broken while open makes
// only its breaking sound; one pushed away by a piston while open closes silently, since its block
// is no longer at the position its viewers had open.
class QcContainerSounds
{
public:
    QcContainerSounds(QcServerState &state, QcServerSounds &sounds);

    // Once per tick, after the screens were updated.
    void update();

private:
    // A backpack open in a player's hand: its block and where it sounds.
    struct Held
    {
        QcBlockId block = qcAirBlockId;
        QcVec3 position;
    };

    // The containers open now: placed ones by position, held backpacks by their holder's name.
    struct Open
    {
        QHash<QcBlockPos, QcBlockId> placed;
        QHash<QString, Held> held;
    };

    [[nodiscard]] Open openNow() const;
    // Whether the block at pos is still the same block type as block (in any state).
    [[nodiscard]] bool isSameBlockAt(QcBlockPos pos, QcBlockId block) const;
    // The block of the backpack the player holds open, if any.
    [[nodiscard]] std::optional<QcBlockId> heldBlockOf(const QcServerClient &client) const;
    void play(QcBlockId block, bool opening, const QcVec3 &position);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    Open m_open;
};
