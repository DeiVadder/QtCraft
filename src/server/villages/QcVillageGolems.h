#pragma once

#include "QcCreature.h"

#include <QList>
#include <QSet>

#include <memory>
#include <optional>

class QcContentRegistry;
class QcPathCellTable;
class QcServerWorld;
class QcVillages;
struct QcVillage;

// The iron golems of villages (decision E-415), checked every few seconds by QcMobManager. A golem
// belongs to the village whose area (QcVillage::contains) it stands in; nothing is claimed, so a
// golem that dies, wanders off or is carried away stops counting by itself, as in the reference
// game. A village whose villagers are awake and whose chunks around its area are all loaded counts
// its golems (QcVillage::golems); with at least `minVillagers` villagers, fewer golems than one per
// `villagersPerGolem` villagers and the last golem `cooldownSeconds` ago (QcVillage::lastGolem), it
// spawns one where a golem stands within `spawnRadius` of its meeting point, below the golems of
// the world (QcServerLimits villageGolems: those every village counted last, and those loaded
// outside villages). The golem is the creature kind of
// QcVillageGolemSettings::mob; while the content has no such kind the rule rests (logged once).
class QcVillageGolems
{
public:
    // Columns tried for a golem's place around the meeting point, and heights above and below it.
    static constexpr int placeAttempts = 12;
    static constexpr int placeHeight = 6;

    struct Spawn
    {
        qint64 village = 0;
        int type = 0;
        QcVec3 position;
    };

    struct Context
    {
        QcServerWorld &world;
        const QcPathCellTable &cells;
        double worldTime = 0.0;
        quint64 seed = 0;
        // Golems in the world at most.
        qint64 worldLimit = 0;
    };

    explicit QcVillageGolems(std::shared_ptr<const QcContentRegistry> content);

    // The golem's creature kind; none while the content lacks it (logged the first time).
    std::optional<int> kind() const;
    // Counts the golems of the villages in `villages` and the golems to spawn; the spawning
    // villages' lastGolem is set.
    QList<Spawn> check(const QList<QcCreature> &mobs, const QSet<qint64> &villages, const Context &context) const;
    // Golems of kind `type` in the area of `village`.
    static int golemsIn(const QcVillage &village, const QList<QcCreature> &mobs, int type);
    // Whether `village` spawns a golem now with `golems` of them in its area; a last golem after
    // `worldTime` (the time went back) counts as long ago.
    bool due(const QcVillage &village, int golems, double worldTime) const;

    // A place where a golem of `type` stands near the meeting point, if one is found: columns drawn
    // within spawnRadius, each tried from the meeting point's height outwards.
    std::optional<QcVec3> placeFor(const QcVillage &village, int type, const Context &context) const;
    // Golems of kind `type` in the world: outside villages those loaded, in each village those it
    // counted last or those loaded in its area, whichever is more (QcVillages::golems).
    static qint64 golemsInWorld(const QList<QcCreature> &mobs, int type, const QcVillages &villages);

private:
    // Every chunk of the village's area and reach is loaded: its golems are all in the world.
    static bool loaded(const QcServerWorld &world, const QcVillage &village);
    // Counts the golems of a loaded village; a last golem after the world time (it went back) is
    // forgotten.
    static void recount(QcVillage &village, const QList<QcCreature> &mobs, int type, const Context &context);
    // The feet of a golem of `type` in `feet`: on dry ground that does not hurt, the whole body
    // clear of blocks.
    std::optional<QcVec3> standsAt(QcBlockPos feet, int type, const Context &context) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    mutable bool m_missingLogged = false;
};
