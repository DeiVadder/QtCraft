#pragma once

#include "QcServerLimits.h"
#include "QcBlockAccess.h"
#include "QcContentRegistry.h"
#include "QcMobManager.h"

#include <QList>

#include <memory>

// Breeding of peaceful animals (decision E-293), on the timers of QcCreature. An adult fed
// with one of its `breeding.food` falls in love (unless it is in love already or still resting
// after breeding); two awake animals of a kind in love within `partnerDistance` that see each other
// get a young one at the feet of one of them and rest for `cooldownSeconds`. Feeding a young one
// brings it closer to growing up. Timers only run while the animal is awake (its chunk loaded).
class QcMobBreeding
{
public:
    // Share of the time to grow up that feeding a young one takes off.
    static constexpr double feedGrowUpShare = 0.1;
    // No young one is born while this many animals of the kind (awake or dormant) live in the 3 x 3
    // chunks around the birth place or this many bred animals of the kind live in the whole world,
    // by default (QcServerLimits::breedingCrowd, bredPerKind).
    static constexpr int crowdLimit = int(QcServerLimits{}.breedingCrowd);
    static constexpr int maxBredPerKind = int(QcServerLimits{}.bredPerKind);
    // Seconds an animal is in love before it pairs (others see it in love first).
    static constexpr double courtshipSeconds = 1.0;

    enum class Fed { No, Love, Young };

    void setLimits(qint64 crowd, qint64 bredPerKind)
    {
        m_crowdLimit = crowd;
        m_bredPerKind = bredPerKind;
    }

    struct Birth
    {
        int type = 0;
        QcVec3 position;
    };

    explicit QcMobBreeding(std::shared_ptr<const QcContentRegistry> content);

    // Whether `mob` would eat the item named `item` now (in love or growing up).
    bool wouldEat(const QcCreature &mob, const QString &item) const;
    // Feeds `mob` with the item named `item`; what happened (No: it does not eat that now).
    Fed feed(QcCreature &mob, const QString &item) const;
    // Advances the timers by `dt` seconds and pairs animals in love; returns the young ones to spawn.
    QList<Birth> tick(const QcBlockAccess &world, QcMobManager &mobs, double dt) const;
    // Whether another young one of `type` may be born at `position`.
    bool roomFor(const QList<QcCreature> &mobs, int type, const QcVec3 &position) const;

private:
    // Animals in love look for a partner within partnerDistance times this ...
    static constexpr double seekFactor = 2.0;
    // ... and stop walking towards it this close.
    static constexpr double closeEnough = 1.5;

    void approachPartners(QList<QcCreature> &mobs) const;
    // In love for at least courtshipSeconds and awake.
    bool courting(const QcCreature &mob) const;
    // The partner index for mob `index` in love, -1 for none.
    qsizetype partnerOf(const QcBlockAccess &world, const QList<QcCreature> &mobs, qsizetype index) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    qint64 m_crowdLimit = crowdLimit;
    qint64 m_bredPerKind = maxBredPerKind;
};
