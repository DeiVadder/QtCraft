#pragma once

#include "server/core/QcServerLimits.h"
#include "server/entities/QcExperienceOrb.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <functional>

class QcBlockAccess;

// The experience orbs of a world (decision E-398), the table of their kind in the entity register.
// Orbs fall and float like dropped items, fly towards the nearest living player within
// attractRange, are collected one every collectIntervalTicks per player after a short pickup
// delay, and go after despawnSeconds in loaded chunks. Only orbs in loaded chunks move and age.
//
// The number of orbs is bounded (QcServerLimits::experienceOrbs): orbs close together merge into one
// worth their sum every mergeIntervalTicks, and experience dropped while the world is full joins the
// nearest orb instead of making a new one, so no experience is lost to the bound.
class QcExperienceOrbManager
{
public:
    static constexpr double size = 0.5;
    static constexpr double despawnSeconds = 300.0;
    static constexpr double pickupDelay = 0.5;
    // Orbs fly towards a living player closer than this, faster the closer he is.
    static constexpr double attractRange = 8.0;
    // Acceleration in m/s² towards a player right next to the orb; it falls off with the square of
    // the distance's share of attractRange (the reference's pull, scaled to this game's gravity).
    static constexpr double attractAcceleration = 90.0;
    // Orbs touch a player when they are within the player's box grown by this much.
    static constexpr double pickupReach = 0.5;
    // A player collects at most one orb every this many ticks (the reference's two).
    static constexpr int collectIntervalTicks = 2;
    static constexpr double mergeRadius = 1.0;
    static constexpr int mergeIntervalTicks = 10;
    // At most this many orbs from one drop; the last one takes the rest.
    static constexpr int maxOrbsPerDrop = 16;
    // Biggest value of one orb, so a merged orb still fits the protocol and the save comfortably;
    // orbs merge and take overflow only while their sum stays within it.
    static constexpr int maxValue = 1 << 24;
    // The bound by default (QcServerLimits::experienceOrbs).
    static constexpr qsizetype defaultLimit = qsizetype(QcServerLimits{}.experienceOrbs);

    struct PlayerView
    {
        quint32 entityId = 0;
        QcVec3 position;
        bool alive = true;
    };

    // Called when a player collects an orb, with its value.
    using GiveFunction = std::function<void(quint32 playerEntityId, int points)>;
    using IdFunction = std::function<quint32()>;

    explicit QcExperienceOrbManager(quint64 seed);

    // Drops `points` at `position` as orbs of the reference's sizes with a small random velocity
    // (a mined ore, a killed creature, a furnace's output, a death). Beyond the limit the points
    // join the nearest orb with room. Returns how many orbs appeared.
    int drop(int points, const QcVec3 &position, const IdFunction &newId);
    // Adds one orb as it is (tests, a chunk coming back); false and nothing added beyond the limit
    // or in the void.
    bool add(QcExperienceOrb orb);

    // One tick: physics and attraction, collecting, merging, ageing and despawning.
    void tick(const QcBlockAccess &world, const QList<PlayerView> &players, double dt, const GiveFunction &give);

    const QList<QcExperienceOrb> &orbs() const { return m_orbs; }
    // For moving and removing orbs from outside (pistons push them, chunks unload).
    QList<QcExperienceOrb> &editableOrbs() { return m_orbs; }
    qsizetype limit() const { return m_limit; }
    void setLimit(qsizetype limit) { m_limit = limit; }
    // The points in all orbs (tests and measurements: merging and the bound keep them).
    qint64 totalPoints() const;
    // Orbs that merged into others since the start.
    qint64 mergedCount() const { return m_merged; }
    // Adds the orbs of a chunk that came back (decision E-374), with new ids; returns how many were
    // left out beyond the limit (their points join the nearest orb with room).
    qsizetype adopt(const QList<QcExperienceOrb> &orbs, const IdFunction &newId);
    void clear();

private:
    void step(QcExperienceOrb &orb, const QList<PlayerView> &players, const QcBlockAccess &world, double dt) const;
    // The living player nearest to `orb` within attractRange; null when none.
    static const PlayerView *nearestPlayer(const QcExperienceOrb &orb, const QList<PlayerView> &players);
    bool collect(QcExperienceOrb &orb, const QList<PlayerView> &players, const GiveFunction &give);
    // Orbs of the merge grid by cell (cellKey in the .cpp), as indices into m_orbs.
    using MergeGrid = QHash<quint64, QList<qsizetype>>;
    void merge();
    // An earlier orb within mergeRadius of `position` in the grid that can take `value` more, or null.
    QcExperienceOrb *mergeTarget(const MergeGrid &grid, const QcVec3 &position, int value);
    // The orb nearest to `position` (awake ones first, however far: decision E-399) that can take
    // `value` more, or null when there is none.
    QcExperienceOrb *nearestOrb(const QcVec3 &position, int value);
    // Adds `value` to the nearest orb with room (the world is full); false when none has room.
    bool joinNearest(const QcVec3 &position, int value);

    QRandomGenerator m_random;
    QList<QcExperienceOrb> m_orbs;
    qsizetype m_limit = defaultLimit;
    quint64 m_ticks = 0;
    // The tick from which each player may collect again.
    QHash<quint32, quint64> m_nextCollect;
    qint64 m_merged = 0;
};
