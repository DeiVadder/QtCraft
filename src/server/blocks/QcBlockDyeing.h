// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/core/QcBlockPos.h"

class QcServerBlocks;
struct QcBlockDefinition;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// A dye used on a block that takes colours (decision E-765): the block's `dyeProperty` (the colour
// of the signal indicator light) becomes the dye's colour (QcItemDefinition::dye), and the player
// gives one dye (not in creative mode). On a sign with text, the dye colours the text instead
// (decision E-830). Made for one use, like QcOpeners.
class QcBlockDyeing
{
public:
    QcBlockDyeing(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerInventory &inventory);

    // Dyes the clicked block within reach; false when the held item is no dye, the block takes no
    // colour or not this one (then other uses of the item may follow). The same colour again takes
    // no dye but still counts as used.
    bool dye(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held);
    // Colours the text of the clicked sign (decision E-830) when the held item is a dye of a sign
    // colour, the sign has text and the player may write on it (QcServerSigns::mayWrite); false
    // otherwise (then the sign opens its editor). The colour the text already has takes no dye
    // but still counts as used.
    bool dyeSign(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held);

private:
    // Plays the dye's sound at `target` and takes one dye from the player (not in creative mode).
    void useDye(QcServerClient &client, QcBlockPos target, const QcBlockDefinition &block);

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
