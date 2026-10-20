#pragma once

#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcBlockPos.h"

class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// A dye used on a block that takes colours (decision E-765): the block's `dyeProperty` (the colour
// of the signal indicator light) becomes the dye's colour (QcItemDefinition::dye), and the player
// gives one dye (not in creative mode). Made for one use, like QcOpeners.
class QcBlockDyeing
{
public:
    QcBlockDyeing(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerInventory &inventory);

    // Dyes the clicked block within reach; false when the held item is no dye, the block takes no
    // colour or not this one (then other uses of the item may follow). The same colour again takes
    // no dye but still counts as used.
    bool dye(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held);

private:
    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
