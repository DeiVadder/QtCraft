#pragma once

#include "server/core/QcServerLimits.h"
#include "server/projectiles/QcProjectileFlight.h"

#include <QList>

#include <functional>
#include <memory>

class QcBlockAccess;
class QcContentRegistry;

// The projectiles of a world (decision E-425), the table of their kind in the entity register.
// Flying ones follow QcProjectileFlight and hit bodies or stick in blocks; stuck ones fall again
// when their block changes, can be picked up by players and go after their definition's
// despawnSeconds; flying ones go after flightSeconds and in the void. Only projectiles in loaded
// chunks move and age.
//
// Bounds (QcServerLimits): at most `limit` projectiles in the world (a launch beyond is refused, the
// launcher keeps its ammunition) and `perChunk` stuck in one chunk (the oldest stuck there goes).
class QcProjectileManager
{
public:
    static constexpr qsizetype defaultLimit = qsizetype(QcServerLimits{}.projectiles);
    static constexpr qsizetype defaultPerChunk = qsizetype(QcServerLimits{}.projectilesPerChunk);
    // A player picks up a stuck projectile when it is within his box grown by this much.
    static constexpr double pickupReach = 0.5;
    // A projectile that hits a body during its short invulnerability bounces back with this share
    // of its velocity, like the reference's arrows.
    static constexpr double bounceFactor = -0.1;

    // A shot (a player's launcher, a creature's brain through QcMobBrain::Context::launches).
    struct Launch
    {
        quint32 shooter = 0;
        // Name of the QcProjectileDefinition.
        QString type;
        // Where its centre starts and its velocity (m/s).
        QcVec3 position;
        QcVec3 velocity;
        QcProjectilePickup pickup = QcProjectilePickup::Item;
        double damageFactor = 1.0;
    };

    struct Hit
    {
        const QcProjectile &projectile;
        const QcProjectileDefinition &definition;
        quint32 body = 0;
        double damage = 0.0;
    };

    // What the tick reports to the server: a hit (true when the body took it; otherwise the
    // projectile bounces, unless it breaks), a stuck projectile a player touches (true when he took
    // it), a projectile that stuck this tick, one that broke at a hit (decision E-427).
    struct Handlers
    {
        std::function<bool(const Hit &)> hit;
        std::function<bool(quint32 player, const QcProjectile &)> pickup;
        std::function<void(const QcProjectile &, const QcProjectileDefinition &)> stuck;
        std::function<void(const QcProjectile &, const QcProjectileDefinition &)> broken;
    };

    struct PlayerView
    {
        quint32 entityId = 0;
        QcAabb box;
    };

    explicit QcProjectileManager(const std::shared_ptr<const QcContentRegistry> &content);

    // Adds the shot with the id `id`; false (nothing added) beyond the limit, for an unknown type,
    // a speed beyond QcProjectileDefinition::maxSpeed or in the void.
    bool launch(const Launch &shot, quint32 id);
    // Adds one projectile as it is (tests, a chunk coming back); false beyond the limit.
    bool add(QcProjectile projectile);

    // One tick of every projectile that does not rest: flight against the blocks and `bodies`,
    // unsticking, pickup by `players`, ageing and despawning.
    void tick(const QcBlockAccess &world, const QList<QcProjectileFlight::Body> &bodies, const QList<PlayerView> &players, double dt,
              const Handlers &handlers);

    const QList<QcProjectile> &all() const { return m_projectiles; }
    QList<QcProjectile> &editableAll() { return m_projectiles; }
    // Whether any projectile flies (and needs the bodies of the tick).
    bool anyFlying() const;
    qsizetype limit() const { return m_limit; }
    void setLimit(qsizetype limit) { m_limit = limit; }
    qsizetype perChunk() const { return m_perChunk; }
    void setPerChunk(qsizetype perChunk) { m_perChunk = perChunk; }
    // Projectiles that went for the per-chunk bound since the start.
    qint64 crowdedOut() const { return m_crowdedOut; }
    // Adds the projectiles of a chunk that came back (decision E-374), with new ids; returns how
    // many were left out beyond the limits.
    qsizetype adopt(const QList<QcProjectile> &projectiles, const std::function<quint32()> &newId);
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
