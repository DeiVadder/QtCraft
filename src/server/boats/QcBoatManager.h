#pragma once

#include "common/core/QcAabb.h"
#include "common/world/QcBlockAccess.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcBoat.h"

#include <QList>

#include <optional>

class QcBoxGrid;
class QcServerWorld;

// The boats of a world (decision E-920), the table of their kind in the entity register: adding,
// finding and removing them within the world's limit, and the step of the boats nobody drives: they
// push each other apart, living bodies push them, and each moves by the shared boat physics. A
// driven boat is stepped with its driver's inputs instead (QcServerBoats::steer), so it moves as the
// driver's client predicts; nothing but blocks stops it. A boat that came to rest, still on the
// water or on the ground (QcBoat::Rest), is not stepped until something pushes or moves it, a
// rider gets in or its blocks change.
class QcBoatManager
{
public:
    // Boats in the world by default (QcServerLimits::maxBoats).
    static constexpr int maxBoats = static_cast<int>(QcServerLimits{}.maxBoats);
    // How fast an overlapping boat or body pushes a boat nobody drives out of a full overlap (m/s
    // added per tick, less for a smaller overlap).
    static constexpr double pushPerTick = 0.3;
    // A boat nobody drives, floating or on the ground, comes to rest below this speed (m/s) and turn
    // (degrees per tick); what is left of both would carry it less than a millimetre.
    static constexpr double restSpeed = 1e-3;
    static constexpr double restSpin = 1e-3;

    static QcAabb bodyOf(const QcBoat &boat);

    const QList<QcBoat> &all() const { return m_boats; }
    // For moving boats from outside (pistons, riders); never add or remove through it.
    QList<QcBoat> &editableAll() { return m_boats; }
    QcBoat *find(quint32 id);
    const QcBoat *find(quint32 id) const;
    // The boat the player (entity id) drives; null for none.
    QcBoat *drivenBy(quint32 player);
    const QcBoat *drivenBy(quint32 player) const;
    // The boat the creature (entity id) sits in; null for none.
    const QcBoat *carrying(quint32 creature) const;
    // False when the limit is reached.
    bool add(const QcBoat &boat);
    std::optional<QcBoat> remove(quint32 id);
    qsizetype limit() const { return m_maxBoats; }
    void setLimit(qsizetype limit) { m_maxBoats = limit; }
    // Adds the boats of a chunk that came back (decision E-374); returns how many were left out at
    // the limit.
    qsizetype adopt(const QList<QcBoat> &boats);

    // Pushes between boats and from `bodies` (boxes of living bodies that sit in no boat) on the
    // boats nobody drives, then steps each of them one tick. Boats whose chunk is not loaded
    // (dormant) and boats at rest wait.
    void tick(const QcServerWorld &world, const QList<QcAabb> &bodies);
    // How many boats the latest tick stepped (neither driven, dormant nor at rest).
    qsizetype lastStepped() const { return m_stepped; }

private:
    // The push on every boat nobody drives from the boats and `bodies` it overlaps, by table index.
    [[nodiscard]] QList<QcVec3> pushes(const QList<QcAabb> &bodies) const;
    // The push on the body `self` of `boxes` from the others of `grid` it overlaps: away from each
    // along the ground, as much as the share of the overlap.
    static QcVec3 pushOn(qsizetype self, const QList<QcAabb> &boxes, const QcBoxGrid &grid);
    // How the boat rests at its place now (blocks read from `world`).
    static QcBoat::Rest restOf(const QcBoat &boat, const QcServerWorld &world);
    // The boat's rest when it still rests: not driven, not pushed, not moved, and its blocks the same
    // (read again only when a block of their chunks changed since); nothing when it has to move.
    static std::optional<QcBoat::Rest> keptRest(const QcBoat &boat, const QcVec3 &push, const QcServerWorld &world);
    // Whether the boat, after its step without driver or push, may rest: floating or on the ground,
    // nearly without speed and turn.
    static bool canRest(const QcBoat &boat);

    QList<QcBoat> m_boats;
    qsizetype m_maxBoats = maxBoats;
    qsizetype m_stepped = 0;
};
