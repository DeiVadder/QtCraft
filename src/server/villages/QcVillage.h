#pragma once

#include "common/core/QcBlockPos.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

// A village as the server knows it (decision E-403): where it is, its beds and workstations and
// who lives there. A generated village starts from the generator's plan (its id is the plan's
// region); a settlement (decision E-490) starts from a bed or workstation placed away from every
// village, its id from that first spot, and has no plan. Blocks placed and broken keep both up to
// date, and beds and workstations placed near a village make its area grow (QcVillages).
// Villagers (decision E-411) claim a bed and a workstation and count as population; children need
// free beds and golems enough population (QcVillagerBreeding, QcVillageGolems), both bounded by
// QcServerLimits.
struct QcVillage
{
    // A bed (its head) or workstation; `claimedBy` is the persistent id (QcCreature::uid, decision
    // E-420) of the villager that uses it (0: free).
    struct Spot
    {
        QcBlockPos at;
        // The kind of work of a workstation (QcVillageSettings::workstations); empty for a bed.
        QString kind;
        quint64 claimedBy = 0;

        bool operator==(const Spot &) const = default;
    };

    // Beds and workstations this far outside the generated area still belong to the village.
    static constexpr int reach = QcVillagePlan::reach;

    // What fromJson() accepts: the box at most `extent` blocks wide (a plan never leaves its
    // region, so the region's edge), each spot list cut to its own limit.
    struct ReadLimits
    {
        int extent = 1024;
        qsizetype beds = 64;
        qsizetype workstations = 64;
    };

    // The region the generator planned it in (QcVillagePlan::id), or for a settlement settlementId()
    // of its first spot.
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
    // Players who hurt its villagers or golems and the world time the grudge ends: its golems attack
    // them (QcGolemGrudges, decision E-471).
    struct Grudges
    {
        // By player name, so leaving and joining again keeps it.
        QHash<QString, double> until;

        // Passing state, neither saved nor compared: villages that differ only in grudges are equal.
        bool operator==(const Grudges &) const { return true; }
    };
    Grudges grudges;
    // The persistent ids of the villagers its population counts, known once its claims were
    // reconciled in this run (QcVillagePopulation): a villager that was away then (its claims lapsed)
    // is not among them, so it neither leaves the population again nor stays a villager here.
    struct Residents
    {
        bool known = false;
        QSet<quint64> uids;

        // Passing state, neither saved nor compared.
        bool operator==(const Residents &) const { return true; }
    };
    Residents residents;

    bool operator==(const QcVillage &) const = default;

    // The id of a settlement founded at `first` (decision E-490): bit 62 set, bit 63 clear, the
    // column packed below (31 bits each for x and z, offset by QcWorld::maxHorizontalBlock, so every
    // column of the world within its technical bound has its own id; decision E-883). No two
    // settlements share a column (QcVillages::clashes), so the height is left out. Region ids
    // (QcVillagePlan::regionId: bit 62 clear) never look so, and no id is 0 (no village).
    static qint64 settlementId(QcBlockPos first)
    {
        constexpr int bits = 31;
        constexpr quint64 mask = (quint64{1} << bits) - 1;
        static_assert(qint64{QcWorld::maxChunkBlock} + QcWorld::maxHorizontalBlock <= static_cast<qint64>(mask));
        constexpr qint64 offset = QcWorld::maxHorizontalBlock;
        const auto x = static_cast<quint64>(first.x + offset) & mask;
        const auto z = static_cast<quint64>(first.z + offset) & mask;
        return static_cast<qint64>((quint64{1} << 62) | (x << bits) | z);
    }
    static bool isSettlement(qint64 id) { return id > 0 && (id >> 62) == 1; }
    bool settlement() const { return isSettlement(id); }

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
