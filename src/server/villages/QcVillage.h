// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

/*! \brief A village as the server knows it (decision E-403): where it is, its beds and workstations
    and who lives there.

    A generated village starts from the generator's plan (its id is the plan's region); a
    settlement (decision E-490) starts from a bed or workstation placed away from every village,
    its id from that first spot, and has no plan. Blocks placed and broken keep both up to date,
    and beds and workstations placed near a village make its area grow (QcVillages). Villagers
    (decision E-411) claim a bed and a workstation and count as population; children need free
    beds and golems enough population (QcVillagerBreeding, QcVillageGolems), both bounded by
    QcServerLimits. */
struct QcVillage
{
    /*! \brief A bed (its head) or workstation; \c {claimedBy} is the persistent id
        (QcCreature::uid, decision E-420) of the villager that uses it (0: free). */
    struct Spot
    {
        /*! Where it is. */
        QcBlockPos at;
        /*! The kind of work of a workstation (QcVillageSettings::workstations); empty for a bed. */
        QString kind;
        /*! The persistent id of the villager that uses it (0: free). */
        quint64 claimedBy = 0;

        bool operator==(const Spot &) const = default;
    };

    /*! Beds and workstations this far outside the generated area still belong to the village. */
    static constexpr int reach = QcVillagePlan::reach;

    /*! \brief What fromJson() accepts: the box at most \c {extent} blocks wide (a plan never
        leaves its region, so the region's edge), each spot list cut to its own limit. */
    struct ReadLimits
    {
        /*! The widest box in blocks. */
        int extent = 1024;
        /*! The most beds. */
        qsizetype beds = 64;
        /*! The most workstations. */
        qsizetype workstations = 64;
    };

    /*! The region the generator planned it in (QcVillagePlan::id), or for a settlement
        settlementId() of its first spot. */
    qint64 id = 0;
    /*! Its village style (QcVillageSettings). */
    QString style;
    /*! The meeting point (the bell of the centre). */
    QcBlockPos centre;
    /*! The box of the generated pieces and roads; with \c {reach} the area whose beds and
        workstations count. \qcshared */
    int minX = 0;
    int minY = 0;
    int minZ = 0;
    int maxX = 0;
    int maxY = 0;
    int maxZ = 0;
    /*! Its beds. */
    QList<Spot> beds;
    /*! Its workstations. */
    QList<Spot> workstations;
    /*! Villagers that live here (decision E-411). */
    int population = 0;
    /*! The iron golems in its area when it last counted them (decision E-415). */
    int golems = 0;
    /*! The world time its last golem came (negative: none yet; decision E-415). */
    double lastGolem = -1.0;
    /*! \brief Players who hurt its villagers or golems and the world time the grudge ends: its
        golems attack them (QcGolemGrudges, decision E-471). */
    struct Grudges
    {
        /*! By player name, so leaving and joining again keeps it. */
        QHash<QString, double> until;

        /*! Passing state, neither saved nor compared: returns true, villages that differ only in
            grudges are equal. */
        bool operator==(const Grudges &) const { return true; }
    };
    /*! Its grudges against players. */
    Grudges grudges;
    /*! \brief The persistent ids of the villagers its population counts, known once its claims
        were reconciled in this run (QcVillagePopulation): a villager that was away then (its
        claims lapsed) is not among them, so it neither leaves the population again nor stays a
        villager here. */
    struct Residents
    {
        /*! Whether the claims were reconciled in this run. */
        bool known = false;
        /*! The persistent ids. */
        QSet<quint64> uids;

        /*! Passing state, neither saved nor compared: returns true. */
        bool operator==(const Residents &) const { return true; }
    };
    /*! Its residents. */
    Residents residents;

    bool operator==(const QcVillage &) const = default;

    /*! Returns the id of a settlement founded at \a first (decision E-490): bit 62 set, bit 63
        clear, the column packed below (31 bits each for x and z, offset by
        QcWorld::maxHorizontalBlock, so every column of the world within its technical bound has
        its own id; decision E-883). No two settlements share a column (QcVillages::clashes), so
        the height is left out. Region ids (QcVillagePlan::regionId: bit 62 clear) never look so,
        and no id is 0 (no village). */
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
    /*! Returns whether \a id is that of a settlement (settlementId()). */
    static bool isSettlement(qint64 id) { return id > 0 && (id >> 62) == 1; }
    /*! Returns whether this village is a settlement. */
    bool settlement() const { return isSettlement(id); }

    /*! Returns whether \a pos lies in its box grown by \c {reach}. */
    bool contains(QcBlockPos pos) const
    {
        return pos.x >= minX - reach && pos.x <= maxX + reach && pos.y >= minY - reach && pos.y <= maxY + reach && pos.z >= minZ - reach
               && pos.z <= maxZ + reach;
    }
    /*! Returns the number of beds no villager claims. */
    qsizetype freeBeds() const;
    /*! Returns the bed or workstation of \a spots at \a at, if listed. */
    static Spot *spotAt(QList<Spot> &spots, QcBlockPos at)
    {
        for (Spot &spot : spots) {
            if (spot.at == at) {
                return &spot;
            }
        }
        return nullptr;
    }

    /*! Returns the village as saved with the world. */
    QJsonObject toJson() const;
    /*! Returns the village saved in \a json; std::nullopt for an entry that is not plausible (a
        box wider than \c {extent} of \a limits); beds and workstations are cut to the most that
        \a limits allows. */
    [[nodiscard]] static std::optional<QcVillage> fromJson(const QJsonObject &json, const ReadLimits &limits);
};
