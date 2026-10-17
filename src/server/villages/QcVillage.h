#pragma once

#include "QcBlockPos.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

// A village as the server knows it (decision E-403): where it is, its beds and workstations and
// who lives there. The generator's plan gives the first state; blocks placed and broken by players
// keep it up to date (QcVillages). Villagers (decision E-411) claim a bed and a workstation and count
// as population; children need free beds and golems enough population (QcVillagerBreeding,
// QcVillageGolems), both bounded by QcServerLimits.
struct QcVillage
{
    // A bed (its head) or workstation; `claimedBy` names the villager that uses it (empty: free).
    struct Spot
    {
        QcBlockPos at;
        // The kind of work of a workstation (QcVillageSettings::workstations); empty for a bed.
        QString kind;
        QString claimedBy;

        bool operator==(const Spot &) const = default;
    };

    // Beds and workstations this far outside the generated area still belong to the village.
    static constexpr int reach = 16;

    // What fromJson() accepts: the box at most `extent` blocks wide (a plan never leaves its
    // region, so the region's edge), each spot list cut to its own limit.
    struct ReadLimits
    {
        int extent = 1024;
        qsizetype beds = 64;
        qsizetype workstations = 64;
    };

    // The region the generator planned it in (QcVillagePlan::id).
    qint64 id = 0;
    QString style;
    // The meeting point (the bell of the centre).
    QcBlockPos centre;
    // The box of the generated pieces and roads; with `reach` the area whose beds and workstations
    // count.
    int minX = 0;
    int minY = 0;
    int minZ = 0;
    int maxX = 0;
    int maxY = 0;
    int maxZ = 0;
    QList<Spot> beds;
    QList<Spot> workstations;
    // Villagers that live here (decision E-411) and the iron golems in its area when it last
    // counted them, with the world time its last golem came (negative: none yet; decision E-415).
    int population = 0;
    int golems = 0;
    double lastGolem = -1.0;
    // The number in the name of the next villager (QcVillagerRecord::name), so names stay unique.
    int nextVillager = 1;

    bool operator==(const QcVillage &) const = default;

    bool contains(QcBlockPos pos) const
    {
        return pos.x >= minX - reach && pos.x <= maxX + reach && pos.y >= minY - reach && pos.y <= maxY + reach && pos.z >= minZ - reach
               && pos.z <= maxZ + reach;
    }
    qsizetype freeBeds() const;
    // The bed or workstation at `at`, if listed.
    static Spot *spotAt(QList<Spot> &spots, QcBlockPos at)
    {
        for (Spot &spot : spots) {
            if (spot.at == at) {
                return &spot;
            }
        }
        return nullptr;
    }

    QJsonObject toJson() const;
    // std::nullopt for an entry that is not plausible (a box wider than `limits.extent` among them);
    // beds and workstations are cut to their limits.
    [[nodiscard]] static std::optional<QcVillage> fromJson(const QJsonObject &json, const ReadLimits &limits);
};
