#pragma once

#include "common/core/QcBlockPos.h"
#include "common/physics/QcPhysics.h"
#include "server/pathing/QcMobNavigator.h"

#include <QHash>

#include <optional>

class QcContentRegistry;
class QcPathCellTable;
struct QcCreature;

// How creatures that open doors (QcPathRules::opensDoors, decision E-423) go through them: they open
// the closed wooden door on their path ahead (QcMobNavigator::Navigation::door) and close the one
// they opened once they are through, and they take turns at a door, one at a time (decision E-411).
// The door a creature opened is its own state (`opened`, a villager's QcVillager::openedDoor); what
// it does to the world comes back as Moves for the caller to carry out.
class QcDoorPassage
{
public:
    // How close a creature comes to a door to open it.
    static constexpr double reach = 1.6;
    // A door counts as passed once the creature is this far from its cell (horizontally).
    static constexpr double passed = 1.5;
    // Creatures this close to a door on their way take turns at it; one that waits steps back to
    // this distance.
    static constexpr double queue = 2.5;
    static constexpr double stepBack = 1.8;

    // Doors to close and to open, in this order.
    struct Moves
    {
        std::optional<QcBlockPos> close;
        std::optional<QcBlockPos> open;
    };

    // Where the creature walks: the world, the path cells of its blocks and the creature's rules.
    struct Ground
    {
        const QcBlockAccess &world;
        const QcPathCellTable &cells;
        QcPathRules rules;
    };

    explicit QcDoorPassage(const QcContentRegistry &content);

    // A new tick: the turns of the last one decide.
    void startTick();
    // One door, one creature at a time: of the creatures in a doorway or within `queue` of a door on
    // their next waypoint, one goes through (the one in the doorway, else the one already going,
    // else the nearest, as of the last tick); the others wait, stepping back out of the doorway
    // where the ground allows. The steering of one that waits; none for one that goes or has no
    // door ahead (or flees).
    std::optional<QcPhysics::Steering> waitAtDoor(QcCreature &mob, const QcMobNavigator::Navigation &navigation, const Ground &ground);
    // Opens the door on the path ahead and closes the one it went through.
    static Moves use(const QcCreature &mob, std::optional<QcBlockPos> &opened, const QcMobNavigator::Navigation &navigation);

private:
    // The door it stands in (`inside`), else a door, open or closed, on its next waypoint.
    std::optional<QcBlockPos> doorAhead(const QcCreature &mob, const QcMobNavigator::Navigation &navigation, const QcBlockAccess &world,
                                        bool &inside) const;
    // A waiting creature steps back and aside to stepBack from the door, facing it.
    static QcPhysics::Steering stepAside(QcCreature &mob, const QcVec3 &doorCentre, double speed, const Ground &ground);

    const QcContentRegistry &m_content;
    // Who goes through a door first: the creature nearest to it, of the last tick and of this one.
    struct Passer
    {
        quint32 id = 0;
        double distance = 0.0;
        bool before(Passer other) const { return distance < other.distance || (distance == other.distance && id < other.id); }
    };
    QHash<QcBlockPos, Passer> m_doorways;
    QHash<QcBlockPos, Passer> m_nextDoorways;
};
