#pragma once

#include "QcBlockDefinition.h"
#include "QcItemDefinition.h"
#include "QcMessages.h"

class QcServerBlocks;
class QcServerCircuits;
class QcServerCombat;
class QcServerFarming;
class QcServerInventory;
class QcBlockScreens;
class QcServerSounds;
class QcServerStands;
struct QcServerClient;
struct QcServerState;

// What players do to the world: mining blocks, using blocks and items (opening machines, toggling
// doors, switching levers, tilling, placing blocks, scooping liquids) and using creatures (feeding,
// catching).
class QcServerInteraction
{
public:
    // Ticks of slack (of 50 ms) when checking a client's claim that a block is broken (network
    // jitter); at a slower tick rate the latest tick's real time counts as slack too, since the
    // players' time only moves with the ticks (decision E-249).
    static constexpr int miningToleranceTicks = 2;

    struct Parts
    {
        QcServerSounds &sounds;
        QcServerBlocks &blocks;
        QcServerInventory &inventory;
        QcBlockScreens &screens;
        QcServerStands &stands;
        QcServerFarming &farming;
        QcServerCombat &combat;
        QcServerCircuits &circuits;
    };

    QcServerInteraction(QcServerState &state, const Parts &parts);

    void finishMining(QcServerClient &client, QcBlockPos pos);
    void useItem(QcServerClient &client, const QcMsg::UseItem &message);
    void useEntity(QcServerClient &client, quint32 entityId);

private:
    // Opens a machine, chest or crafting table, toggles a door or switches a lever, button or repeater
    // at `target`; false when the click is not such a use.
    bool useClickedBlock(QcServerClient &client, QcBlockPos target);
    // Empties a liquid source into the held scooping item; false when nothing was taken.
    bool scoopLiquid(QcServerClient &client, QcBlockPos pos);
    // Fills a held glass bottle from the liquid at `pos`; the liquid stays.
    bool fillBottle(QcServerClient &client, QcBlockPos pos);
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
