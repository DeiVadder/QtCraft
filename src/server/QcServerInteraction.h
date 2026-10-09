#pragma once

#include "QcBlockDefinition.h"
#include "QcItemDefinition.h"
#include "QcMessages.h"

class QcServerBlocks;
class QcServerInventory;
class QcServerMachines;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// What players do to the world: mining blocks, using blocks and items (opening machines, toggling
// doors, placing blocks, scooping liquids) and catching creatures.
class QcServerInteraction
{
public:
    // Ticks of slack when checking a client's claim that a block is broken (network jitter).
    static constexpr int miningToleranceTicks = 2;

    struct Parts
    {
        QcServerSounds &sounds;
        QcServerBlocks &blocks;
        QcServerInventory &inventory;
        QcServerMachines &machines;
    };

    QcServerInteraction(QcServerState &state, const Parts &parts);

    void finishMining(QcServerClient &client, QcBlockPos pos);
    void useItem(QcServerClient &client, const QcMsg::UseItem &message);
    void useEntity(QcServerClient &client, quint32 entityId);

private:
    // Opens a machine, chest or crafting table or toggles a door at `target`; false when the click
    // is not such a use.
    bool useClickedBlock(QcServerClient &client, QcBlockPos target);
    // Empties a liquid source into the held scooping item; false when nothing was taken.
    bool scoopLiquid(QcServerClient &client, QcBlockPos pos);
    void placeHeld(QcServerClient &client, const QcMsg::UseItem &message, const QcItemDefinition *held);
    // Whether the block may be placed: saplings need suitable ground, chests and machines room in
    // their manager and nobody may end up inside it.
    bool mayPlace(QcServerClient &client, QcBlockId state, QcBlockId placed, QcBlockPos target) const;
    void applyPlacement(QcServerClient &client, const QcItemDefinition &held, QcBlockId state, QcBlockPos target);
    // Resends the true blocks of a refused placement and the inventory.
    void rejectPlacement(QcServerClient &client, QcBlockPos target);

    QcServerState &m_state;
    Parts m_parts;
};
