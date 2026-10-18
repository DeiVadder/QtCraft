#pragma once

#include "server/entities/QcCreature.h"
#include "server/villages/QcVillagerHomes.h"

#include <QList>

#include <functional>
#include <memory>

class QcContentRegistry;
class QcVillages;
struct QcVillage;

// Villagers moving between villages (decision E-490), checked every few seconds by
// QcVillagePopulation. A villager that stands more than QcVillagerHomes::strayReach beyond its
// village's area (carried off in a minecart, pushed or led away) leaves it: its claims are freed and
// the village counts one less. A villager without a bed whose village has none free, and a homeless
// one (no village, a settlement that is gone, or away when its village was reconciled), moves into
// the village with the free bed nearest to it within searchRadius, if that village has more beds
// than villagers, room below the villagers per village, and the villager does not stand beyond its
// area by more than strayReach (it would move out again at once). A bed whose claim lapsed is thus taken by any homeless villager near it,
// not only by a child. Villagers a structure confines (QcCreature::confined) and ones without a mind (QcCreature::noAi)
// never move.
class QcVillagerRelocation
{
public:
    // Homeless villagers find beds this far (horizontally) from where they stand.
    static constexpr int searchRadius = 48;

    struct Context
    {
        QcVillages &villages;
        QcVillagerHomes::Limits limits;
        // Whether a village takes newcomers: its claims are known (reconciled after loading, or none
        // to reconcile; QcVillagePopulation).
        std::function<bool(QcVillage &)> settled;
    };

    explicit QcVillagerRelocation(std::shared_ptr<const QcContentRegistry> content);

    // Moves the awake villagers that strayed out of their villages and into new homes; the number of
    // villagers that moved out or in.
    int check(QList<QcCreature> &mobs, const Context &context) const;
    // Whether a villager at `at` stands beyond the area of `village` by more than strayReach.
    static bool strayed(const QcVillage &village, const QcVec3 &at);

private:
    // The village (other than `except`) with the free bed nearest to `at` within searchRadius that
    // takes a newcomer; null when there is none.
    static QcVillage *newHome(const Context &context, const QcVec3 &at, qint64 except);

    std::shared_ptr<const QcContentRegistry> m_content;
};
