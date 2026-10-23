#pragma once

#include "server/spawners/QcSpawnerRules.h"
#include "worldgen/generators/QcGuardSite.h"

#include <QHash>
#include <QList>

class QcServerState;

// Guardians that come back to the zones of structures (decision E-591; the sand golem in the cellar
// of a desert temple, a renewable source of sand). Every checkTicks game ticks it asks the world
// generator for the zones within `reach` of each living player (QcWorldGenerator::plannedStructureSitesNear,
// from its plans, never planning on the tick thread: around a player whose regions are not all
// planned yet they are planned on a generation thread and the zones come with a later check; nothing
// is saved) and gives a zone a guardian when all of these hold: fewer than the
// zone's `max` creatures of its kind guard it; its cooldown ran out (it starts when the zone was last
// full or last got one; a zone seen for the first time in a session waits half its cooldown, so
// reloading the world at most halves the time between two guardians); no living player is
// within `minPlayerDistance` of the zone and one is within `maxPlayerDistance`; every chunk of the
// zone is loaded; the world holds fewer than QcServerLimits::guardians guardians. The guardian comes
// into the first of `attempts` random cells of the zone that has room for its body, solid ground and
// block light at most the zone's `maxLight` (QcSpawnerRules::fits), and keeps to the zone
// (QcCreature::guards). Kinds a spawner may not bring forth (QcSpawnerContent::refusal) never come.
// The game rule mobSpawning does not matter (like spawners).
class QcGuardians
{
public:
    static constexpr int checkTicks = 20;
    // How far from a player zones are looked for (the largest `maxPlayerDistance` a zone may have).
    static constexpr int reach = 128;
    // Random cells of the zone tried per guardian.
    static constexpr int attempts = 8;

    QcGuardians(QcServerState &state, const QcSpawnerRules &cells);

    void tick();
    // Awake and resting guardians in the world, and those of one zone.
    int guardianCount() const;
    int guardianCount(quint64 site) const;
    // The game tick from which the zone may next get a guardian; std::nullopt before it was seen.
    [[nodiscard]] std::optional<quint64> readyAt(quint64 site) const;

private:
    // Gives the zone a guardian when its rule allows one now (`total`: guardians in the world so
    // far); whether it did.
    [[nodiscard]] bool serve(const QcGuardSite &site, const QList<QcVec3> &players, int total);
    // Whether the players are where the zone's rule wants them.
    static bool playersAllow(const QcGuardSite &site, const QList<QcVec3> &players);
    bool zoneLoaded(const QcGuardSite &site) const;
    // A guardian of kind `type` in a random fitting cell of the zone; false when none fit.
    bool bringForth(const QcGuardSite &site, int type);
    QList<QcVec3> livingPlayers() const;

    QcServerState &m_state;
    const QcSpawnerRules &m_cells;
    QHash<quint64, quint64> m_readyAt;
};
