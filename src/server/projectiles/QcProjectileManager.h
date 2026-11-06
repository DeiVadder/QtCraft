// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/core/QcServerLimits.h"
#include "server/projectiles/QcProjectileFlight.h"

#include <QList>

#include <functional>
#include <memory>

class QcBlockAccess;
class QcContentRegistry;

/*! \brief The projectiles of a world (decision E-425), the table of their kind in the entity
    register.

    Flying ones follow QcProjectileFlight and hit bodies or stick in blocks; stuck ones fall again
    when their block changes, can be picked up by players and go after their definition's
    despawnSeconds; flying ones go after flightSeconds and in the void. Only projectiles in loaded
    chunks move and age.

    Bounds (QcServerLimits): at most \c {limit} projectiles in the world (a launch beyond is
    refused, the launcher keeps its ammunition) and \c {perChunk} stuck in one chunk (the oldest
    stuck there goes). */
class QcProjectileManager
{
public:
    /*! The default of limit(), QcServerLimits::projectiles. */
    static constexpr qsizetype defaultLimit = static_cast<qsizetype>(QcServerLimits{}.projectiles);
    /*! The default of perChunk(), QcServerLimits::projectilesPerChunk. */
    static constexpr qsizetype defaultPerChunk = static_cast<qsizetype>(QcServerLimits{}.projectilesPerChunk);
    /*! A player picks up a stuck projectile when it is within their box grown by this much. */
    static constexpr double pickupReach = 0.5;
    /*! A projectile that hits a body during its short invulnerability bounces back with this
        share of its velocity, like the reference's arrows. */
    static constexpr double bounceFactor = -0.1;

    /*! \brief A shot (a player's launcher, a creature's brain through
        QcMobBrain::Context::launches). */
    struct Launch
    {
        /*! Entity id of the shooter (0 for none). */
        quint32 shooter = 0;
        /*! Name of the QcProjectileDefinition. */
        QString type;
        /*! Where its centre starts. */
        QcVec3 position;
        /*! Its velocity (m/s). */
        QcVec3 velocity;
        /*! QcProjectile::pickup. */
        QcProjectilePickup pickup = QcProjectilePickup::Item;
        /*! QcProjectile::damageFactor. */
        double damageFactor = 1.0;
        /*! QcProjectile::transient. */
        bool transient = false;
    };

    /*! \brief A projectile that hit a body this tick.

        \qcfields The \c {projectile} with its \c {definition}, the entity id of the \c {body} it
        hit and the \c {damage} it deals there (QcProjectileFlight::damageAt). */
    struct Hit
    {
        const QcProjectile &projectile;
        const QcProjectileDefinition &definition;
        quint32 body = 0;
        double damage = 0.0;
    };

    /*! \brief A projectile met a block (a target reads where, decision E-768).

        \qcfields The \c {block} and its \c {face} it met, the \c {point} of contact, and
        \c {stuck} for one that sticks there (an arrow), else it breaks or goes. */
    struct BlockHit
    {
        QcBlockPos block;
        QcBlockFace face = QcBlockFace::Up;
        QcVec3 point;
        bool stuck = false;
    };

    /*! \brief What the tick reports to the server.

        \qcfields \c {hit}: a hit (true when the body took it; otherwise the projectile bounces,
        unless it breaks); \c {pickup}: a stuck projectile a player touches (true when they took
        it); \c {stuck}: a projectile that stuck this tick; \c {broken}: one that broke at a hit
        (decision E-427); \c {blockHit}: every block a projectile met (optional). */
    struct Handlers
    {
        std::function<bool(const Hit &)> hit;
        std::function<bool(quint32 player, const QcProjectile &)> pickup;
        std::function<void(const QcProjectile &, const QcProjectileDefinition &)> stuck;
        std::function<void(const QcProjectile &, const QcProjectileDefinition &)> broken;
        std::function<void(const BlockHit &)> blockHit;
    };

    /*! \brief A player as the tick sees them for pickups.

        \qcfields The player's \c {entityId} and collision \c {box}. */
    struct PlayerView
    {
        quint32 entityId = 0;
        QcAabb box;
    };

    explicit QcProjectileManager(const std::shared_ptr<const QcContentRegistry> &content);

    /*! Adds \a shot with the id \a id; returns false (nothing added) beyond the limit, for an
        unknown type, a speed beyond QcProjectileDefinition::maxSpeed or in the void. */
    bool launch(const Launch &shot, quint32 id);
    /*! Adds \a projectile as it is (tests, a chunk coming back); returns false (nothing added)
        beyond the limit or in the void (QcWorld::isInVoid). */
    bool add(QcProjectile projectile);

    /*! One tick of \a dt seconds of every projectile that does not rest: flight against the
        blocks of \a world and \a bodies, unsticking, pickup by \a players, ageing and despawning;
        what happens goes to \a handlers. */
    void tick(const QcBlockAccess &world, const QList<QcProjectileFlight::Body> &bodies, const QList<PlayerView> &players, double dt,
              const Handlers &handlers);

    /*! Returns every projectile of the world. */
    const QList<QcProjectile> &all() const { return m_projectiles; }
    /*! Returns the projectile \a id, nullptr when there is none. */
    QcProjectile *find(quint32 id);
    /*! Returns every projectile of the world for changes in place (loading, tests). */
    QList<QcProjectile> &editableAll() { return m_projectiles; }
    /*! Returns whether any projectile flies (and needs the bodies of the tick). */
    bool anyFlying() const;
    /*! Returns the most projectiles the world holds. */
    qsizetype limit() const { return m_limit; }
    /*! Sets limit() to \a limit. */
    void setLimit(qsizetype limit) { m_limit = limit; }
    /*! Returns the most stuck projectiles in one chunk. */
    qsizetype perChunk() const { return m_perChunk; }
    /*! Sets perChunk() to \a perChunk. */
    void setPerChunk(qsizetype perChunk) { m_perChunk = perChunk; }
    /*! Returns the projectiles that went for the per-chunk bound since the start. */
    qint64 crowdedOut() const { return m_crowdedOut; }
    /*! Adds \a projectiles of a chunk that came back (decision E-374), with new ids from
        \a newId; returns how many were left out beyond the limits. */
    qsizetype adopt(const QList<QcProjectile> &projectiles, const std::function<quint32()> &newId);
    /*! Removes every projectile. */
    void clear() { m_projectiles.clear(); }

private:
    // What every projectile of one tick shares.
    struct Pass
    {
        const QcBlockAccess &world;
        const QList<QcProjectileFlight::Body> &bodies;
        double dt = 0.0;
        const Handlers &handlers;
    };

    // One projectile's tick; false when it goes.
    static bool update(QcProjectile &projectile, const QcProjectileDefinition &definition, const Pass &pass);
    static bool fly(QcProjectile &projectile, const QcProjectileDefinition &definition, const Pass &pass);
    static bool touches(const QcProjectile &projectile, const PlayerView &player);
    // Removes the oldest stuck projectiles of the chunks in `chunks` beyond the per-chunk bound.
    void thinChunks(const QList<QcChunkPos> &chunks);

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<QcProjectile> m_projectiles;
    qsizetype m_limit = defaultLimit;
    qsizetype m_perChunk = defaultPerChunk;
    qint64 m_crowdedOut = 0;
};
