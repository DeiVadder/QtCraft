#include "common/content/parser/QcContentParser.h"
#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "QcTestWorld.h"
#include "server/gameplay/QcBodyBurning.h"
#include "server/mobs/QcArcherBrain.h"
#include "server/mobs/QcMobSteering.h"
#include "server/plants/QcServerPlants.h"
#include "server/projectiles/QcProjectileFlight.h"
#include "server/storage/QcEntityRecords.h"
#include "server/weather/QcWeatherCycle.h"

#include <QJsonDocument>

#include <numbers>

using namespace Qt::StringLiterals;

// Skeleton archers (decision E-426): distance, line of sight, draw time, cooldown and damage of
// their arrows, aim and lead, burning in daylight (also zombies), drops, bone meal, natural
// spawning, unloading mid-draw and the drawn bow on the client. Ticks are stepped by hand.
class TestArchers : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double dt = QcPhysics::tickDuration;
    static constexpr double noon = QcDayCycle::dayLength * 10.25;
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    // Half the edge of the flat arena around the player.
    static constexpr int arena = 22;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }
    const QcMobDefinition &skeletonKind() const { return m_content->mobs()[type("skeleton")]; }
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

    quint32 spawn(const char *name, int dx, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, double(m_feet.y), m_feet.z + dz + 0.5};
        return m_server->entities().creatures().spawn(type(name), at, m_server->entities().newId());
    }

    double distanceTo(quint32 archer)
    {
        const QcVec3 offset = mob(archer)->motion.position - player().motion.position;
        return std::hypot(offset.x, offset.z);
    }

    // The ticks (counted from 0) at which arrows of `shooter` appeared within `count` ticks.
    QList<int> shotTicks(quint32 shooter, int count)
    {
        QList<int> shots;
        QSet<quint32> seen;
        for (int tick = 0; tick < count; ++tick) {
            ticks(1);
            for (const QcProjectile &projectile : m_server->entities().projectiles().all()) {
                if (projectile.shooter == shooter && !seen.contains(projectile.id)) {
                    seen.insert(projectile.id);
                    shots.append(tick);
                }
            }
        }
        return shots;
    }

    // Creatures of `kind` that appear around a player on the generated surface within 30 s at `time`.
    int spawned(const char *kind, double time, bool surfaceOnly)
    {
        QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int x = -5; x <= 5; ++x) {
            for (int z = -5; z <= 5; ++z) {
                world.loadChunk({x, z});
            }
        }
        const QcVec3 player{0.5, double(world.chunk({0, 0})->highestNonAir(0, 0) + 1), 0.5};
        QcMobManager mobs(m_content, 7);
        quint32 nextId = 100;
        QSet<quint32> seen;
        for (int tick = 0; tick < 20 * 30; ++tick) {
            mobs.tick(world, {{7, player, true}}, time, dt, [&nextId] { return nextId++; });
            for (const QcCreature &creature : mobs.mobs()) {
                if (creature.type == type(kind) && (!surfaceOnly || !creature.underground)) {
                    seen.insert(creature.id);
                }
            }
        }
        return int(seen.size());
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void skeletonsComeFromData()
    {
        const QcMobDefinition &skeleton = skeletonKind();
        QCOMPARE(skeleton.ai, QcMobDefinition::Ai::Archer);
        QVERIFY(skeleton.hostile && skeleton.burnsInDaylight);
        QCOMPARE(skeleton.archer.projectile, u"qtcraft:arrow"_s);
        QCOMPARE(skeleton.heldItem, u"qtcraft:bow"_s);
        QVERIFY(m_content->mobs()[type("zombie")].burnsInDaylight);

        // An archer needs its settings and they need the archer AI.
        const auto parse = [](const char *json) {
            const QcResult<QcMobDefinition> mob = QcContentParser::parseMob(QJsonDocument::fromJson(json).object(), u"qtcraft"_s);
            if (!mob) {
                qInfo().noquote() << mob.error();
            }
            return mob;
        };
        QVERIFY(parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow"}})").has_value());
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer"})").has_value());
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"archer":{"projectile":"arrow"}})").has_value());
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","ai":"archer","archer":{"projectile":"arrow"}})").has_value());
        QVERIFY(!parse(R"({"name":"a","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"archer","archer":{"projectile":"arrow","minDistance":9,"keepDistance":8}})")
                     .has_value());
    }

    void aimLeadsAMovingTargetAndLandsOnIt()
    {
        // A flight of the aimed velocity in an empty world passes the target, standing or moving.
        const QcProjectileDefinition &arrow = *m_content->projectile(u"qtcraft:arrow"_s);
        QcTestWorld world(*m_content);
        for (const QcVec3 &velocity : {QcVec3{}, QcVec3{0.0, 0.0, 4.0}}) {
            const QcVec3 from{0.0, 1.6, 0.0};
            const QcVec3 target{12.0, 0.9, 3.0};
            QcProjectile projectile;
            projectile.type = arrow.name;
            projectile.motion.position = from;
            projectile.motion.velocity = QcArcherBrain::aim(from, target, velocity, 25.0, arrow);
            double closest = 1e9;
            for (int tick = 1; tick < 40; ++tick) {
                QcProjectileFlight::step(world, projectile, arrow, {}, dt);
                closest = std::min(closest, (projectile.motion.position - (target + velocity * (tick * dt))).length());
            }
            QVERIFY2(closest < 0.3, qPrintable(QString::number(closest)));
        }

        // The spread turns, but keeps the speed and stays within its angle.
        QRandomGenerator random(3);
        const QcVec3 straight{20.0, 0.0, 0.0};
        for (int i = 0; i < 50; ++i) {
            const QcVec3 spread = QcArcherBrain::spread(straight, 2.0, random);
            QVERIFY(std::abs(spread.length() - 20.0) < 1e-9);
            QVERIFY(std::acos(spread.dot(straight) / 400.0) <= 2.0 * std::numbers::pi / 180.0 + 1e-9);
        }
    }

    void shootsAStandingPlayerAfterItsDrawWithCooldown()
    {
        joinArena();
        const quint32 archer = spawn("skeleton", 9, 3);
        const QList<int> shots = shotTicks(archer, 130);
        QVERIFY2(shots.size() >= 3, qPrintable(QString::number(shots.size())));

        // It sees the player within a look (10 ticks) and then draws one second before each shot;
        // after a shot it waits one second before it draws again.
        const int drawTicks = int(std::lround(skeletonKind().archer.drawSeconds / dt));
        const int cooldownTicks = int(std::lround(skeletonKind().archer.cooldownSeconds / dt));
        QVERIFY2(shots[0] >= drawTicks && shots[0] <= drawTicks + QcMobSteering::sightCheckTicks, qPrintable(QString::number(shots[0])));
        for (qsizetype i = 1; i < shots.size(); ++i) {
            const int gap = shots[i] - shots[i - 1];
            QVERIFY2(gap >= drawTicks + cooldownTicks && gap <= drawTicks + cooldownTicks + 2, qPrintable(QString::number(gap)));
        }

        // Every arrow hit: 2 per 20 m/s at about 25 m/s, rounded up, is 3 each, through no armour.
        QCOMPARE(player().health, QcServerClient::maxHealth - 3.0 * double(shots.size()));
        QVERIFY(m_client->health < QcServerClient::maxHealth);
    }

    void archersArrowsCannotBePickedUp()
    {
        joinArena();
        // A wall behind the player stops the arrows that miss an invulnerable player.
        chat(u"/gamemode creative"_s);
        const quint32 archer = spawn("skeleton", 9, 0);
        mob(archer)->chasing = player().id;
        mob(archer)->targetMemory = 60.0;
        ticks(30);
        const QList<QcProjectile> &all = m_server->entities().projectiles().all();
        // A creative player is no target: no shot at all.
        QVERIFY(all.isEmpty());

        chat(u"/gamemode survival"_s);
        QVERIFY(!shotTicks(archer, 60).isEmpty());
        for (const QcProjectile &projectile : m_server->entities().projectiles().all()) {
            QCOMPARE(projectile.pickup, QcProjectilePickup::Never);
            QCOMPARE(projectile.shooter, archer);
        }
    }

    void keepsItsDistance()
    {
        joinArena();
        // Too close: it backs away to at least minDistance.
        const quint32 close = spawn("skeleton", 2, 0);
        ticks(80);
        QVERIFY2(distanceTo(close) > skeletonKind().archer.minDistance - 0.5, qPrintable(QString::number(distanceTo(close))));
        m_server->entities().creatures().remove(close);
        player().health = QcServerClient::maxHealth;

        // Far, but in sight: it comes within keepDistance and stays out of minDistance.
        const quint32 far = spawn("skeleton", 15, 2);
        ticks(100);
        const double distance = distanceTo(far);
        QVERIFY2(distance <= skeletonKind().archer.keepDistance + 0.5 && distance >= skeletonKind().archer.minDistance - 0.5,
                 qPrintable(QString::number(distance)));
    }

    void shootsOnlyWithALineOfSight()
    {
        joinArena();
        // A wall between them: it never sees the player, so it neither draws nor shoots.
        for (int dz = -arena; dz <= arena; ++dz) {
            for (int dy = 0; dy <= 6; ++dy) {
                m_server->world().setBlock(m_feet.offset(4, dy, dz), id("qtcraft:stone"));
            }
        }
        const quint32 archer = spawn("skeleton", 9, 0);
        for (int tick = 0; tick < 60; ++tick) {
            ticks(1);
            QVERIFY(!mob(archer)->archer->drawing());
        }
        QVERIFY(m_server->entities().projectiles().all().isEmpty());
        QCOMPARE(m_client->flags.value(archer) & QcMsg::Drawing, 0);

        // The wall gone, it draws and shoots; the client sees the drawn bow in its hand.
        for (int dz = -arena; dz <= arena; ++dz) {
            for (int dy = 0; dy <= 6; ++dy) {
                m_server->world().setBlock(m_feet.offset(4, dy, dz), qcAirBlockId);
            }
        }
        QVERIFY(!shotTicks(archer, 50).isEmpty());
        QVERIFY((m_client->flags.value(archer) & QcMsg::Drawing) != 0);
        QCOMPARE(m_client->held.value(archer), item("qtcraft:bow"));
    }

    void burnsInDirectSunlightOnly()
    {
        joinArena(noon);
        chat(u"/gamemode creative"_s);
        const auto burns = [this](const char *kind, int dx, int dz) {
            const quint32 id = spawn(kind, dx, dz);
            ticks(1);
            const bool burning = mob(id)->burnTicks > 0;
            m_server->entities().creatures().remove(id);
            return burning;
        };

        // Out in the open by day, both kinds catch fire for sunlightTicks.
        QVERIFY(burns("skeleton", 6, 6));
        QVERIFY(burns("zombie", -6, 6));
        const quint32 lasting = spawn("skeleton", 6, -6);
        ticks(1);
        QCOMPARE(mob(lasting)->burnTicks, QcBodyBurning::sunlightTicks);
        m_server->entities().creatures().remove(lasting);
        // Pigs do not.
        QVERIFY(!burns("pig", 0, 8));

        // A roof directly above the head block is shade; glass there lets the sky through.
        const int aboveHead = int(std::floor(skeletonKind().height * 0.9)) + 1;
        QCOMPARE(aboveHead, 2);
        m_server->world().setBlock(m_feet.offset(8, aboveHead, -8), id("qtcraft:stone"));
        QVERIFY(!burns("skeleton", 8, -8));
        m_server->world().setBlock(m_feet.offset(-4, aboveHead, 8), id("qtcraft:glass"));
        QVERIFY(burns("skeleton", -4, 8));
        // Standing in water.
        m_server->world().setBlock(m_feet.offset(-8, 0, -8), id("qtcraft:water"));
        m_server->world().setBlock(m_feet.offset(-8, 1, -8), id("qtcraft:water"));
        QVERIFY(!burns("zombie", -8, -8));

        // At night nothing burns; in the rain neither.
        m_server->setWorldTime(midnight);
        QVERIFY(!burns("skeleton", 6, 6));
        m_server->setWorldTime(noon);
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather rain"_s);
        ticks(QcWeatherCycle::fadeTicks + 2);
        QVERIFY(!burns("skeleton", 6, 6));
    }

    void dropsBonesAndArrows()
    {
        joinArena();
        const quint32 archer = spawn("skeleton", 6, 0);
        const std::optional<QList<QcItemDrop>> drops = m_server->entities().creatures().damage(archer, 100.0, player().motion.position);
        QVERIFY(drops);
        QStringList items;
        for (const QcItemDrop &drop : *drops) {
            items.append(drop.item);
        }
        QVERIFY(items.contains(u"qtcraft:bone"_s) && items.contains(u"qtcraft:arrow"_s));

        // Chances of creature drops come from the data (the zombie's feather).
        const QList<QcItemDrop> &zombie = m_content->mobs()[type("zombie")].drops;
        const auto feather = std::ranges::find(zombie, u"qtcraft:feather"_s, &QcItemDrop::item);
        QVERIFY(feather != zombie.end());
        QCOMPARE(feather->chance, 0.25);
    }

    void boneMealGrowsCropsAndSaplings()
    {
        joinArena(QcDayCycle::dayLength * 10.25);
        const QcItemId meal = item("qtcraft:bone_meal");
        player().inventory.add(meal, 64, *m_content);
        const QcBlockPos crop = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(crop.offset(0, -1, 0), id("qtcraft:farmland"));
        m_server->world().setBlock(crop, id("qtcraft:wheat_crop"));
        const QcBlockProperty *ages = m_content->block(id("qtcraft:wheat_crop")).property(u"age"_s);
        QVERIFY(ages);
        const auto age = [&] { return ages->values.indexOf(m_content->property(*m_server->world().blockAt(crop), u"age"_s)); };
        const auto held = [&] {
            int count = 0;
            for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
                count += player().inventory.slot(slot).item == meal ? player().inventory.slot(slot).count : 0;
            }
            return count;
        };

        QCOMPARE(age(), 0);
        const int before = held();
        send(QcMsg::UseItem{crop, QcBlockFace::Up});
        QVERIFY2(age() >= QcServerPlants::fertilizeStagesMin && age() <= QcServerPlants::fertilizeStagesMax, qPrintable(QString::number(age())));
        QCOMPARE(held(), before - 1);
        // Until ripe, then it takes no more.
        for (int use = 0; use < 5; ++use) {
            send(QcMsg::UseItem{crop, QcBlockFace::Up});
        }
        QCOMPARE(age(), ages->values.size() - 1);
        const int ripe = held();
        send(QcMsg::UseItem{crop, QcBlockFace::Up});
        QCOMPARE(held(), ripe);

        // A sapling grows into its tree after a few uses.
        const QcBlockPos sapling = m_feet.offset(-3, 0, 2);
        m_server->world().setBlock(sapling, id("qtcraft:oak_sapling"));
        int uses = 0;
        while (m_server->world().blockAt(sapling) == id("qtcraft:oak_sapling") && uses < 30) {
            send(QcMsg::UseItem{sapling, QcBlockFace::Up});
            ticks(1);
            ++uses;
        }
        QVERIFY2(m_server->world().blockAt(sapling) == id("qtcraft:oak_log"), qPrintable(QString::number(uses)));
    }

    void spawnAtNightAndInTheDark()
    {
        QVERIFY(spawned("skeleton", QcDayCycle::dayLength * 0.75, true) > 0);
        QCOMPARE(spawned("skeleton", QcDayCycle::dayLength * 0.25, true), 0);
        const QcMobDefinition &skeleton = skeletonKind();
        QVERIFY(skeleton.spawnsInCaves && skeleton.spawnTime == QcMobDefinition::SpawnTime::Night);
    }

    void drawEndsWithoutSightAndDoesNotResume()
    {
        joinArena();
        const int drawTicks = int(std::lround(skeletonKind().archer.drawSeconds / dt));
        const QcVec3 at{m_feet.x + 9.5, double(m_feet.y), m_feet.z + 0.5};
        const quint32 archer = m_server->entities().creatures().spawn(type("skeleton"), at, m_server->entities().newId(), false, true);
        const auto untilDrawing = [&](quint32 id) {
            return QTest::qWaitFor(
                [&] {
                    ticks(1);
                    return mob(id)->archer.has() && mob(id)->archer->drawing();
                },
                5000);
        };
        QVERIFY(untilDrawing(archer));

        // A wall between them: the draw ends at once, and no arrow comes while the wall stands.
        const auto wall = [this](QcBlockId block) {
            for (int dz = -arena; dz <= arena; ++dz) {
                for (int dy = 0; dy <= 6; ++dy) {
                    m_server->world().setBlock(m_feet.offset(4, dy, dz), block);
                }
            }
        };
        const QcVec3 wallAt = mob(archer)->motion.position;
        wall(id("qtcraft:stone"));
        QVERIFY2(wallAt.x > m_feet.x + 4.5, "the archer stays behind the wall");
        ticks(1);
        QVERIFY(!mob(archer)->archer->drawing());
        QVERIFY(shotTicks(archer, drawTicks * 2).isEmpty());
        wall(qcAirBlockId);
        QVERIFY(untilDrawing(archer));

        // Its chunk leaves mid-draw and comes back (a persistent archer is saved): the draw starts
        // over, so its first shot takes the whole draw time.
        const QcChunkPos chunk = QcEntities::chunkOf(*mob(archer));
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::take(m_server->entities(), {chunk});
        QVERIFY(!mob(archer));
        QVERIFY(records.contains(chunk));
        QSet<quint32> before;
        for (const QcCreature &creature : m_server->entities().creatures().mobs()) {
            before.insert(creature.id);
        }
        QVERIFY(QcEntityRecords::apply(records[chunk], m_server->entities()).isEmpty());
        quint32 back = 0;
        for (const QcCreature &creature : m_server->entities().creatures().mobs()) {
            if (!before.contains(creature.id) && creature.type == type("skeleton") && creature.persistent) {
                back = creature.id;
            }
        }
        QVERIFY(back != 0);
        QVERIFY(!mob(back)->archer.has() || !mob(back)->archer->drawing());
        const QList<int> shots = shotTicks(back, drawTicks * 3);
        QVERIFY(!shots.isEmpty());
        QVERIFY2(shots.front() >= drawTicks, qPrintable(QString::number(shots.front())));
    }

    void aStrikeBreaksTheDraw()
    {
        joinArena();
        const int drawTicks = int(std::lround(skeletonKind().archer.drawSeconds / dt));
        const quint32 archer = spawn("skeleton", 9, 0);
        QVERIFY(QTest::qWaitFor(
            [&] {
                ticks(1);
                return mob(archer)->archer->drawing();
            },
            5000));

        // The player steps next to it mid-draw: it strikes, the draw ends with the blow, and no arrow
        // leaves the bow before a whole new draw.
        int strikeTick = -1;
        QSet<quint32> seen;
        for (int tick = 0; tick < drawTicks * 4; ++tick) {
            const QcVec3 at = mob(archer)->motion.position;
            player().motion.position = {at.x + 1.0, at.y, at.z};
            player().health = QcServerClient::maxHealth;
            ticks(1);
            if (mob(archer)->attackedThisTick) {
                strikeTick = strikeTick < 0 ? tick : strikeTick;
                QVERIFY(!mob(archer)->archer->drawing());
            }
            for (const QcProjectile &projectile : m_server->entities().projectiles().all()) {
                if (projectile.shooter == archer && !seen.contains(projectile.id)) {
                    seen.insert(projectile.id);
                    QVERIFY(strikeTick >= 0);
                    QVERIFY2(tick - strikeTick >= drawTicks, qPrintable(QString::number(tick - strikeTick)));
                }
            }
        }
        QVERIFY(strikeTick >= 0);
    }

    void fastAndJumpingTargetsKeepTheArrowDamage()
    {
        joinArena();
        const QcArcherDefinition &settings = skeletonKind().archer;
        const quint32 archer = spawn("skeleton", 0, 12);
        QSet<quint32> seen;
        const auto checkArrows = [&] {
            for (const QcProjectile &projectile : m_server->entities().projectiles().all()) {
                if (projectile.shooter != archer || seen.contains(projectile.id) || projectile.motion.velocity.length() <= 0.0) {
                    continue;
                }
                seen.insert(projectile.id);
                // Launched at any speed, it hits as hard as one at `speed`.
                const double designed = projectile.damageFactor * projectile.motion.velocity.length();
                QVERIFY2(std::abs(designed - settings.speed) < settings.speed * 0.1, qPrintable(QString::number(designed)));
            }
        };

        // Fleeing sideways at 30 m/s: the lead believes at most maxTargetSpeed.
        for (int tick = 0; tick < 120; ++tick) {
            const double x = ((tick / 12) % 2 == 0 ? -9.0 : 9.0) + ((tick / 12) % 2 == 0 ? 1.5 : -1.5) * (tick % 12);
            player().motion.position = {m_feet.x + 0.5 + x, double(m_feet.y), m_feet.z + 0.5};
            player().health = QcServerClient::maxHealth;
            ticks(1);
            if (mob(archer)->archer.has()) {
                QVERIFY(mob(archer)->archer->targetVelocity.length() <= QcArcherBrain::maxTargetSpeed + 1e-9);
            }
            checkArrows();
        }

        // Jumping 16 m to and fro: each jump starts the estimate at rest.
        for (int tick = 0; tick < 60; ++tick) {
            player().motion.position = {m_feet.x + 0.5 + (tick % 2 == 0 ? -8.0 : 8.0), double(m_feet.y), m_feet.z + 0.5};
            player().health = QcServerClient::maxHealth;
            ticks(1);
            if (tick > 0 && mob(archer)->archer.has() && mob(archer)->archer->target == player().id) {
                QCOMPARE(mob(archer)->archer->targetVelocity, QcVec3{});
            }
            checkArrows();
        }
        QVERIFY2(!seen.isEmpty(), "the archer shot");
    }
};

QTEST_GUILESS_MAIN(TestArchers)
#include "test_archers.moc"
