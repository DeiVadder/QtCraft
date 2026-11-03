// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcCreatureKindId.h"
#include "server/entities/QcCreature.h"
#include "server/villages/QcVillagerHomes.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class QcBlockAccess;
class QcContentRegistry;
class QcVillages;
struct QcVillage;

/*! \brief Villagers getting children (decision E-415), checked every few seconds by
    QcVillagePopulation.

    Farmers holding more produce than \c {shareAbove} share it with villagers of their village
    nearby that lack food (QcVillagerBreedingSettings). Two adults of a village that are willing
    (awake, with a mind (QcCreature::noAi), standing, not fleeing or trading, resting no longer
    after their last child, each holding \c {food} produce) and see each other within \c
    {partnerDistance} get a child when their village breeds now: by day (the routine says neither
    home nor sleep), not in rain at its meeting point, with a free bed and more beds than villagers
    (one whose bed was broken claims the next free one), below the villagers per village and in the
    world (QcServerLimits). Each parent gives \c {food} produce and rests for \c {cooldownSeconds};
    the child (QcVillagePopulation) moves into the free bed nearest to them. At most one child per
    village and check: a village fills its free beds over minutes, and only beds bound its size, as
    in the reference game. */
class QcVillagerBreeding
{
public:
    /*! \brief A child to spawn at \c {position} that moves into \c {bed} of \c {village}. */
    struct Birth
    {
        /*! The village the child moves into. */
        qint64 village = 0;
        /*! The bed it moves into. */
        QcBlockPos bed;
        /*! Where it spawns. */
        QcVec3 position;
        /*! The creature kind of the parents. */
        QcCreatureKindId type;
    };

    /*! \brief What a breeding check reads. */
    struct Context
    {
        /*! The blocks (free beds). */
        const QcBlockAccess &world;
        /*! The villages. */
        QcVillages &villages;
        /*! The world time. */
        double worldTime = 0.0;
        /*! Whether it rains on a block's column (QcVillagerBehaviour::rainsAt). */
        std::function<bool(QcBlockPos)> rains;
        /*! The bounds of the villagers' claims. */
        QcVillagerHomes::Limits limits;
        /*! Villages whose claims were reconciled after loading
            (QcVillagePopulation::checkVillageClaims, decision E-416); only they share food and
            breed. Null: every village. */
        const QSet<qint64> *reconciled = nullptr;
    };

    explicit QcVillagerBreeding(std::shared_ptr<const QcContentRegistry> content);

    /*! Shares food and pairs the willing villagers among \a mobs of every village of \a context;
        returns the births, the parents' food given and their rest begun. The beds are claimed
        when the children settle. */
    QList<Birth> check(QList<QcCreature> &mobs, const Context &context) const;
    /*! Returns whether the villagers of \a village breed now in the tick of \a context (day, dry,
        a free bed and more beds than villagers, below the limits with \a worldPopulation
        villagers in the world). */
    bool breedsNow(const QcVillage &village, const Context &context, qsizetype worldPopulation) const;
    /*! Returns whether \a mob is a willing adult villager. */
    bool willing(const QcCreature &mob) const;

private:
    using Villagers = QHash<qint64, QList<QcCreature *>>;

    // The awake villagers of each village.
    static Villagers villagersByVillage(QList<QcCreature> &mobs);
    // Farmers above shareAbove give the nearest grown-up villager that lacks food what it lacks.
    void share(const QList<QcCreature *> &villagers) const;
    // Two willing villagers that see each other within partnerDistance, if any.
    std::optional<std::pair<QcCreature *, QcCreature *>> pairIn(const QList<QcCreature *> &villagers, const Context &context) const;
    static std::optional<QcBlockPos> nearestFreeBed(const QcVillage &village, const QcVec3 &near);

    std::shared_ptr<const QcContentRegistry> m_content;
};
