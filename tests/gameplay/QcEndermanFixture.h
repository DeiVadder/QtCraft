#pragma once

#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "QcTestWorld.h"
#include "server/mobs/QcNeutralBrain.h"

#include <cmath>
#include <functional>
#include <numbers>

// The common ground of the enderman tests (decision E-460): a flat arena at night around the player,
// spawning creatures in it, turning the player's look, content with other chances to carry blocks,
// and natural spawning on a generated world. Ticks are stepped by hand.
class QcEndermanFixture : public QcServerTestFixture
{
    Q_OBJECT

protected:
    static constexpr double dt = QcPhysics::tickDuration;
    static constexpr double noon = QcDayCycle::dayLength * 10.25;
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    static constexpr int arena = 22;

    bool m_spawnedInsideBlock = false;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    int type(const char *name) const { return *m_content->mobIndex(QStringLiteral("qtcraft:") + QString::fromLatin1(name)); }
    const QcMobDefinition &endermanKind() const { return m_content->mobs()[type("enderman")]; }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }

    // A server at night with a flat, open arena of grass around the player.
    void joinArena(double time = midnight)
    {
        join();
        m_server->setWorldTime(time);
        loadAround(m_feet, 2);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 12; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    quint32 spawn(const char *name, int dx, int dz, bool confined = false)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, double(m_feet.y), m_feet.z + dz + 0.5};
        const quint32 id = m_server->entities().creatures().spawn(type(name), at, m_server->entities().newId());
        mob(id)->confined = confined;
        return id;
    }

    // Turns the player's look towards `point`.
    void lookAt(const QcVec3 &point)
    {
        const QcVec3 eyes = player().motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        const QcVec3 towards = (point - eyes).normalized();
        player().yaw = float(std::atan2(towards.x, -towards.z) * 180.0 / std::numbers::pi);
        player().pitch = float(std::asin(towards.y) * 180.0 / std::numbers::pi);
    }

    QcVec3 headOf(quint32 id) { return mob(id)->motion.position + QcVec3{0.0, endermanKind().height * endermanKind().gaze->headShare, 0.0}; }

    // The base content with the enderman's chances to take and place blocks set (a mod).
    void setCarryChances(double take, double place)
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition enderman = endermanKind();
        enderman.carries->takeChance = take;
        enderman.carries->placeChance = place;
        content->registerMob(enderman);
        m_content = content;
    }

    // Whether a body of `cells` stands at `feet`, read from the blocks directly.
    bool standsSafely(const QcServerWorld &world, const QcVec3 &feet, int cells)
    {
        const QcBlockPos cell = QcPhysics::blockContaining(feet);
        const std::optional<QcBlockId> floor = world.blockAt(cell.offset(0, -1, 0));
        if (!floor || !m_content->block(*floor).solid || m_content->block(*floor).liquid) {
            return false;
        }
        for (int up = 0; up < cells; ++up) {
            if (world.blockAt(cell.offset(0, up, 0)) != qcAirBlockId) {
                return false;
            }
        }
        return true;
    }

    // Grass blocks of the arena at `level` above the floor (-1: the floor).
    int grassAt(int level)
    {
        int count = 0;
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                count += m_server->world().blockAt(m_feet.offset(dx, level, dz)) == id("qtcraft:grass_block") ? 1 : 0;
            }
        }
        return count;
    }

    // Creatures of `kind` that appear around a player on the generated surface within `seconds` at
    // `time`; `build` may change the world first. One that appears without room for its body
    // (three cells) sets m_spawnedInsideBlock.
    QList<QcCreature> spawned(const char *kind, double time, int seconds, const std::function<void(QcServerWorld &)> &build = {})
    {
        QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int x = -5; x <= 5; ++x) {
            for (int z = -5; z <= 5; ++z) {
                world.loadChunk({x, z});
            }
        }
        if (build) {
            build(world);
        }
        const QcVec3 player{0.5, double(world.chunk({0, 0})->highestNonAir(0, 0) + 1), 0.5};
        QcMobManager mobs(m_content, 11);
        quint32 nextId = 100;
        QHash<quint32, QcCreature> seen;
        for (int tick = 0; tick < 20 * seconds; ++tick) {
            mobs.tick(world, {{7, player, true}}, time, dt, [&nextId] { return nextId++; });
            for (const QcCreature &creature : mobs.mobs()) {
                if (creature.type == type(kind) && !seen.contains(creature.id)) {
                    seen.insert(creature.id, creature);
                    // Where it appeared it has room for its body (three cells).
                    const QcBlockPos feet = QcPhysics::blockContaining(creature.motion.position);
                    for (int up = 0; up < 3; ++up) {
                        if (m_content->block(world.blockAt(feet.offset(0, up, 0)).value_or(qcAirBlockId)).solid) {
                            qWarning() << "spawned inside a block at" << feet.x << feet.y << feet.z;
                            m_spawnedInsideBlock = true;
                        }
                    }
                }
            }
        }
        return seen.values();
    }

};
