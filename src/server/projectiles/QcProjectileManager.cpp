#include "server/projectiles/QcProjectileManager.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcBlockAccess.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace {

// The chunk a projectile belongs to (QcEntities::chunkOf).
QcChunkPos chunkOf(const QcProjectile &projectile)
{
    return QcPhysics::blockContaining(projectile.motion.position).chunk();
}

} // namespace

QcProjectileManager::QcProjectileManager(const std::shared_ptr<const QcContentRegistry> &content)
    : m_content(content)
{
}

bool QcProjectileManager::launch(const Launch &shot, quint32 id)
{
    const bool known = m_content->projectile(shot.type) != nullptr;
    if (!known || shot.velocity.length() > QcProjectileDefinition::maxSpeed || !std::isfinite(shot.velocity.length())) {
        return false;
    }

    QcProjectile projectile;
    projectile.id = id;
    projectile.type = shot.type;
    projectile.shooter = shot.shooter;
    projectile.motion.position = shot.position;
    projectile.motion.velocity = shot.velocity;
    projectile.pickup = shot.pickup;
    projectile.damageFactor = shot.damageFactor;
    projectile.transient = shot.transient;
    QcProjectileFlight::turnAlong(projectile);
    return add(std::move(projectile));
}

bool QcProjectileManager::add(QcProjectile projectile)
{
    if (m_projectiles.size() >= m_limit || QcWorld::isInVoid(projectile.motion.position.y)) {
        return false;
    }
    m_projectiles.append(std::move(projectile));
    return true;
}

bool QcProjectileManager::anyFlying() const
{
    return std::ranges::any_of(m_projectiles, [](const QcProjectile &projectile) { return !projectile.stuck && !projectile.dormant; });
}

void QcProjectileManager::tick(const QcBlockAccess &world, const QList<QcProjectileFlight::Body> &bodies, const QList<PlayerView> &players,
                               double dt, const Handlers &handlers)
{
    const Pass pass{world, bodies, dt, handlers};
    QList<QcChunkPos> stuckIn;
    qsizetype kept = 0;
    for (qsizetype i = 0; i < m_projectiles.size(); ++i) {
        QcProjectile &projectile = m_projectiles[i];
        const QcProjectileDefinition *definition = m_content->projectile(projectile.type);
        bool stays = definition != nullptr;
        if (stays && !projectile.dormant) {
            const bool wasStuck = projectile.stuck;
            stays = update(projectile, *definition, pass);
            if (stays && projectile.stuck && !wasStuck) {
                stuckIn.append(chunkOf(projectile));
            }
            for (qsizetype p = 0; stays && projectile.stuck && projectile.pickup != QcProjectilePickup::Never && p < players.size(); ++p) {
                stays = !(touches(projectile, players[p]) && handlers.pickup(players[p].entityId, projectile));
            }
        }
        if (stays) {
            if (kept != i) {
                m_projectiles[kept] = std::move(projectile);
            }
            ++kept;
        }
    }
    m_projectiles.remove(kept, m_projectiles.size() - kept);
    thinChunks(stuckIn);
}

bool QcProjectileManager::update(QcProjectile &projectile, const QcProjectileDefinition &definition, const Pass &pass)
{
    projectile.age += pass.dt;
    if (projectile.stuck) {
        // Its block changed (mined, burnt, moved): it falls again from where it is.
        const std::optional<QcBlockId> state = pass.world.blockAt(projectile.stuckIn);
        if (state && *state != projectile.stuckState) {
            projectile.stuck = false;
            projectile.age = 0.0;
            projectile.motion.velocity = {};
        }
        return projectile.stuck ? projectile.age < definition.despawnSeconds : true;
    }

    if (projectile.age >= definition.flightSeconds || QcWorld::isInVoid(projectile.motion.position.y)) {
        return false;
    }
    return fly(projectile, definition, pass);
}

bool QcProjectileManager::fly(QcProjectile &projectile, const QcProjectileDefinition &definition, const Pass &pass)
{
    const QcProjectileFlight::Step step = QcProjectileFlight::step(pass.world, projectile, definition, pass.bodies, pass.dt);
    switch (step.outcome) {
    case QcProjectileFlight::Outcome::Flying:
    case QcProjectileFlight::Outcome::Waiting:
        return true;
    case QcProjectileFlight::Outcome::HitBody: {
        const double speed = projectile.motion.velocity.length();
        const Hit hit{projectile, definition, step.body, QcProjectileFlight::damageAt(definition, speed, projectile.damageFactor)};
        const bool took = pass.handlers.hit(hit);
        if (definition.breaks()) {
            pass.handlers.broken(projectile, definition);
            return false;
        }
        if (took) {
            return false;
        }
        projectile.motion.velocity = projectile.motion.velocity * bounceFactor;
        QcProjectileFlight::turnAlong(projectile);
        return true;
    }
    case QcProjectileFlight::Outcome::Burnt:
        return false;
    case QcProjectileFlight::Outcome::HitBlock:
        break;
    }

    if (definition.breaks()) {
        pass.handlers.broken(projectile, definition);
        return false;
    }
    if (!definition.sticks) {
        return false;
    }
    projectile.stuck = true;
    projectile.stuckIn = step.block;
    projectile.stuckState = pass.world.blockAt(step.block).value_or(0);
    projectile.motion.velocity = {};
    projectile.age = 0.0;
    pass.handlers.stuck(projectile, definition);
    return true;
}

bool QcProjectileManager::touches(const QcProjectile &projectile, const PlayerView &player)
{
    const QcVec3 reach{pickupReach, pickupReach, pickupReach};
    const QcAabb box{player.box.min - reach, player.box.max + reach};
    const QcVec3 &p = projectile.motion.position;
    return p.x >= box.min.x && p.x <= box.max.x && p.y >= box.min.y && p.y <= box.max.y && p.z >= box.min.z && p.z <= box.max.z;
}

void QcProjectileManager::thinChunks(const QList<QcChunkPos> &chunks)
{
    if (chunks.isEmpty()) {
        return;
    }

    // The stuck projectiles of those chunks, as indices, oldest first.
    QHash<QcChunkPos, QList<qsizetype>> stuck;
    for (const QcChunkPos chunk : chunks) {
        stuck.insert(chunk, {});
    }
    for (qsizetype i = 0; i < m_projectiles.size(); ++i) {
        const QcProjectile &projectile = m_projectiles[i];
        const auto it = projectile.stuck ? stuck.find(chunkOf(projectile)) : stuck.end();
        if (it != stuck.end()) {
            it->append(i);
        }
    }

    QList<qsizetype> gone;
    for (QList<qsizetype> &list : stuck) {
        if (list.size() <= m_perChunk) {
            continue;
        }
        std::ranges::stable_sort(list, [this](qsizetype a, qsizetype b) { return m_projectiles[a].age > m_projectiles[b].age; });
        gone.append(list.mid(0, list.size() - m_perChunk));
    }
    std::ranges::sort(gone, std::greater<>());
    for (const qsizetype index : std::as_const(gone)) {
        m_projectiles.removeAt(index);
    }
    m_crowdedOut += gone.size();
}

qsizetype QcProjectileManager::adopt(const QList<QcProjectile> &projectiles, const std::function<quint32()> &newId)
{
    qsizetype left = 0;
    QList<QcChunkPos> chunks;
    for (QcProjectile projectile : projectiles) {
        projectile.id = newId();
        projectile.shooter = 0;
        if (!add(std::move(projectile))) {
            ++left;
        } else if (m_projectiles.last().stuck) {
            chunks.append(chunkOf(m_projectiles.last()));
        }
    }

    const qint64 before = m_crowdedOut;
    thinChunks(chunks);
    return left + qsizetype(m_crowdedOut - before);
}
