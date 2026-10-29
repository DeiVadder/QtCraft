#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/gameplay/QcAdventureRules.h"
#include "common/protocol/QcMessages.h"

class QcBlockScreens;
class QcFireIgnition;
class QcPlayerHealth;
class QcServerAnimals;
class QcServerBlocks;
class QcServerBoats;
class QcServerCircuits;
class QcServerCrafting;
class QcServerGolems;
class QcServerInventory;
class QcServerKitchen;
class QcServerMinecarts;
class QcServerPlants;
class QcServerSounds;
class QcServerStands;
struct QcServerWorkstations;
struct QcServerClient;
class QcServerState;

// What players do to the world: mining blocks, using blocks and items (opening machines, toggling
// doors, switching levers, tilling, carving, lighting fire, placing blocks and building golems from
// them, scooping liquids) and using creatures (feeding, repairing golems, catching).
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
        QcServerCrafting &crafting;
        QcBlockScreens &screens;
        QcServerStands &stands;
        QcServerMinecarts &minecarts;
        QcServerBoats &boats;
        QcServerAnimals &animals;
        QcServerPlants &plants;
        QcPlayerHealth &health;
        QcServerCircuits &circuits;
        QcFireIgnition &ignition;
        QcServerGolems &golems;
        QcServerKitchen &kitchen;
        QcServerWorkstations &workstations;
    };

    QcServerInteraction(QcServerState &state, const Parts &parts);

    void finishMining(QcServerClient &client, QcBlockPos pos);
    void useItem(QcServerClient &client, const QcMsg::UseItem &message);
    void useEntity(QcServerClient &client, quint32 entityId);
    // While a player mines, the arm swings again every swing length (QcSwingSpeed of his status
    // effects, decision E-881; simulated ticks only), in step with his own client's loop.
    void swingWhileMining();

private:
    // The held item and the creature kind of a use on a creature, read before the use changes them,
    // for the adventure guide (decision E-923); empty when either is missing.
    struct UseOnEntity
    {
        QString item;
        QString entity;
    };
    UseOnEntity useOnEntity(const QcServerClient &client, quint32 entityId) const;
    // Fills a bottle or scoops with a bucket at `target`; a refusal sends the block and the
    // inventory again (the client may have predicted it). Whether it happened.
    bool takeLiquid(QcServerClient &client, QcBlockPos target, const QcItemDefinition &held);
    // Reports a use that did something to the adventure guide: on a creature, or of `held` on the
    // block `clicked` (read before the use changed it).
    void noteUse(QcServerClient &client, const UseOnEntity &use);
    void noteUse(QcServerClient &client, const QcItemDefinition *held, std::optional<QcBlockId> clicked);
    // Whether the player's mode lets the held item do `action` to the block at `pos` (adventure
    // allow-lists, decision E-735); false where no block is loaded.
    bool permits(const QcServerClient &client, QcAdventureRules::Action action, QcBlockPos pos) const;
    // Opens a machine, chest or crafting table, toggles a door, switches a lever, button or repeater,
    // eats cake or cooks on a campfire at `target`; false when the click is not such a use.
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
