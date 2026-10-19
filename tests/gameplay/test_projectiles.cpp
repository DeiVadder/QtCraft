#include "clientcore/session/QcClientLauncher.h"
#include "common/gameplay/QcArmour.h"
#include "QcServerTestFixture.h"
#include "QcTestWorld.h"
#include "server/projectiles/QcPlayerLaunchers.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"

using namespace Qt::StringLiterals;

// Projectiles, bows and arrows (decision E-425): trajectories and range by the power of the draw,
// hits with damage, knockback and armour, no tunnelling at the highest speed, sticking, falling,
// despawning and pickup, the bounds, saving, the void and unloaded chunks, and shooting through the
// protocol. Ticks are stepped by hand.
class TestProjectiles : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double dt = QcPhysics::tickDuration;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    QcProjectileManager &projectiles() { return m_server->entities().projectiles(); }
    const QcProjectileDefinition &arrow() const { return *m_content->projectile(u"qtcraft:arrow"_s); }

    // An arrow shot by nobody from `from` with `velocity`.
    bool shoot(const QcVec3 &from, const QcVec3 &velocity, QcProjectilePickup pickup = QcProjectilePickup::Item)
    {
        return m_server->systems().projectiles.launch({0, u"qtcraft:arrow"_s, from, velocity, pickup});
    }

    // Flies an arrow over open air from height 0 until it falls below it again; the horizontal
    // distance it covered.
    double rangeAt(double power, double pitch) const
    {
        QcTestWorld world(*m_content);
        QcProjectile projectile;
        projectile.type = u"qtcraft:arrow"_s;
        projectile.motion.position = {0.0, 0.0, 0.0};
        projectile.motion.velocity = QcProjectileFlight::direction(0.0, pitch) * (QcLauncherDefinition{}.speed * power);
        for (int tick = 0; tick < 2000; ++tick) {
            QcProjectileFlight::step(world, projectile, arrow(), {}, dt);
            if (projectile.motion.position.y < 0.0) {
                break;
            }
        }
        return std::hypot(projectile.motion.position.x, projectile.motion.position.z);
    }

    // A stone wall across the arrow's way, `distance` blocks east of the player.
    QcBlockPos wallAt(int distance)
    {
        const QcBlockPos wall = m_feet.offset(distance, 1, 0);
        m_server->world().setBlock(wall, id("qtcraft:stone"));
        return wall;
    }

    int slotHolding(QcItemId wanted)
    {
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            if (player().inventory.slot(slot).item == wanted && !player().inventory.slot(slot).isEmpty()) {
                return slot;
            }
        }
        return -1;
    }

    void give(const char *name, int count)
    {
        player().inventory.add(item(name), count, *m_content);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void powerFollowsTheDrawCurve()
    {
        const QcLauncherDefinition bow;
        QCOMPARE(bow.powerAfter(0.0), 0.0);
        QCOMPARE(bow.powerAfter(0.5), (0.25 + 1.0) / 3.0);
        QCOMPARE(bow.powerAfter(1.0), 1.0);
        QCOMPARE(bow.powerAfter(3.0), 1.0);
        // One tick of drawing stays below the minimum power: no shot.
        QVERIFY(bow.powerAfter(dt) < bow.minPower);
    }

    void rangeGrowsWithThePowerOfTheDraw()
    {
        const double quarter = rangeAt(0.25, 45.0);
        const double half = rangeAt(0.5, 45.0);
        const double full = rangeAt(1.0, 45.0);
        QVERIFY2(quarter < half && half < full, qPrintable(u"%1 %2 %3"_s.arg(quarter).arg(half).arg(full)));
        // Drag keeps it below the range in a vacuum (v² / g = 180 m), the reference's bow reaches about 120 m.
        QVERIFY2(full > 100.0 && full < 180.0, qPrintable(QString::number(full)));
        // 30° flies less far than 45°, and a flat shot least.
        QVERIFY(rangeAt(1.0, 30.0) < full);
        QVERIFY(rangeAt(1.0, 5.0) < rangeAt(1.0, 30.0));
    }

    void waterSlowsAndUnloadedChunksHold()
    {
        QcTestWorld world(*m_content);
        world.fillBox({-2, -5, -40}, {2, 5, 2}, id("qtcraft:water"));
        QcProjectile projectile;
        projectile.type = u"qtcraft:arrow"_s;
        projectile.motion.velocity = {0.0, 0.0, -60.0};
        for (int tick = 0; tick < 10; ++tick) {
            QcProjectileFlight::step(world, projectile, arrow(), {}, dt);
        }
        QVERIFY(projectile.motion.inWater);
        QVERIFY2(projectile.motion.velocity.length() < 60.0 * 0.4, qPrintable(QString::number(projectile.motion.velocity.length())));

        // A segment into an unloaded chunk waits where it is, with its velocity.
        world.loadedRadius = 10;
        QcProjectile edge;
        edge.type = u"qtcraft:arrow"_s;
        edge.motion.position = {9.5, 20.0, 0.5};
        edge.motion.velocity = {60.0, 0.0, 0.0};
        QcProjectileFlight::turnAlong(edge);
        const float yaw = edge.yaw;
        const float pitch = edge.pitch;
        for (int tick = 0; tick < 100; ++tick) {
            const QcProjectileFlight::Step step = QcProjectileFlight::step(world, edge, arrow(), {}, dt);
            QCOMPARE(step.outcome, QcProjectileFlight::Outcome::Waiting);
        }
        QCOMPARE(edge.motion.position, QcVec3(9.5, 20.0, 0.5));
        QCOMPARE(edge.motion.velocity, QcVec3(60.0, 0.0, 0.0));
        QCOMPARE(edge.yaw, yaw);
        QCOMPARE(edge.pitch, pitch);
    }

    void noTunnellingAtTheHighestSpeed()
    {
        // A one-block wall and a body 0.1 m thin, at the fastest any launcher may shoot (10 m a tick).
        QcTestWorld world(*m_content);
        world.set({6, 0, 0}, id("qtcraft:stone"));
        const double speed = QcProjectileDefinition::maxSpeed;
        QcProjectile projectile;
        projectile.type = u"qtcraft:arrow"_s;
        projectile.motion.position = {0.5, 0.5, 0.5};
        projectile.motion.velocity = {speed, 0.0, 0.0};
        projectile.age = 1.0;
        QcProjectileFlight::Step step;
        for (int tick = 0; tick < 4 && step.outcome == QcProjectileFlight::Outcome::Flying; ++tick) {
            step = QcProjectileFlight::step(world, projectile, arrow(), {}, dt);
        }
        QCOMPARE(step.outcome, QcProjectileFlight::Outcome::HitBlock);
        QCOMPARE(step.block, QcBlockPos(6, 0, 0));
        QVERIFY(projectile.motion.position.x < 6.0);

        const QList<QcProjectileFlight::Body> bodies{{77, QcAabb{{3.0, 0.0, 0.0}, {3.1, 2.0, 1.0}}}};
        QcProjectile thin;
        thin.type = u"qtcraft:arrow"_s;
        thin.motion.position = {0.5, 0.5, 0.5};
        thin.motion.velocity = {speed, 0.0, 0.0};
        thin.age = 1.0;
        step = QcProjectileFlight::step(world, thin, arrow(), bodies, dt);
        QCOMPARE(step.outcome, QcProjectileFlight::Outcome::HitBody);
        QCOMPARE(step.body, quint32(77));
        QVERIFY(thin.motion.position.x < 3.0);

        // Its shooter is not hit while it leaves him: it flies on to the wall.
        thin = {};
        thin.type = u"qtcraft:arrow"_s;
        thin.shooter = 77;
        thin.motion.position = {0.5, 0.5, 0.5};
        thin.motion.velocity = {speed, 0.0, 0.0};
        QCOMPARE(QcProjectileFlight::step(world, thin, arrow(), bodies, dt).outcome, QcProjectileFlight::Outcome::HitBlock);
    }

    void hitsHurtPushAndBounceDuringInvulnerability()
    {
        join();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const QcVec3 target{m_feet.x + 3.5, double(m_feet.y), m_feet.z - 2.5};
        const quint32 mob = m_server->entities().creatures().spawn(pig, target, m_server->entities().newId());
        const double before = m_server->entities().creatures().find(mob)->health;

        // At 60 m/s an arrow deals 2 * 60 / 20 = 6.
        QVERIFY(shoot({m_feet.x - 1.5, m_feet.y + 0.5, m_feet.z - 2.5}, {60.0, 0.0, 0.0}));
        ticks(2);
        const QcCreature *hit = m_server->entities().creatures().find(mob);
        QVERIFY(hit);
        QCOMPARE(hit->health, before - 6.0);
        QVERIFY(hit->motion.velocity.x > 0.0);
        QVERIFY(projectiles().all().isEmpty());

        // Right after the hit it is invulnerable: the next arrow bounces back and flies on.
        QVERIFY(shoot({m_feet.x + 1.5, m_feet.y + 0.5, m_feet.z - 2.5}, {60.0, 0.0, 0.0}));
        ticks(1);
        QCOMPARE(m_server->entities().creatures().find(mob)->health, before - 6.0);
        QCOMPARE(projectiles().all().size(), 1);
        QVERIFY(projectiles().all().front().motion.velocity.x < 0.0);
    }

    void armourTakesPartOfAHitOnAPlayer()
    {
        join();
        const QcVec3 from{m_feet.x - 2.5, m_feet.y + 1.0, m_feet.z + 0.5};
        QVERIFY(shoot(from, {60.0, 0.0, 0.0}));
        ticks(2);
        QCOMPARE(player().health, QcServerClient::maxHealth - 6.0);
        QVERIFY(player().motion.velocity.x > 0.0);
        QVERIFY(m_client->health < QcServerClient::maxHealth);

        // With a diamond chestplate (8 points: 32 % less) after the invulnerability.
        player().health = QcServerClient::maxHealth;
        player().hurt.clear();
        player().equipment.at(QcEquipmentSlot::Chest) = {item("qtcraft:diamond_chestplate"), 1, 0};
        player().motion.position = {m_feet.x + 0.5, double(m_feet.y), m_feet.z + 0.5};
        QVERIFY(shoot(from, {60.0, 0.0, 0.0}));
        ticks(2);
        const int points = QcArmour::points(*m_content, player().equipment);
        QVERIFY(points > 0);
        QCOMPARE(player().health, QcServerClient::maxHealth - QcArmour::reducedDamage(6.0, points));
    }

    void arrowsStickFallAgainAndDespawn()
    {
        join();
        const QcBlockPos wall = wallAt(3);
        QVERIFY(shoot({m_feet.x + 1.5, m_feet.y + 1.5, m_feet.z + 0.5}, {30.0, 0.0, 0.0}, QcProjectilePickup::Never));
        ticks(3);
        QCOMPARE(projectiles().all().size(), 1);
        const QcProjectile stuck = projectiles().all().front();
        QVERIFY(stuck.stuck);
        QCOMPARE(stuck.stuckIn, wall);
        QVERIFY(stuck.motion.position.x < wall.x && stuck.motion.position.x > wall.x - 0.5);

        // The client sees it stuck.
        ticks(1);
        QVERIFY(((m_client->flags.value(stuck.id) | m_client->spawns.value(stuck.id).flags) & QcMsg::Stuck) != 0);
        QCOMPARE(m_client->spawns.value(stuck.id).kind, QcEntityKind::Projectile);
        QCOMPARE(m_client->spawns.value(stuck.id).item.item, item("qtcraft:arrow"));

        // Its block goes: it falls to the ground and sticks there.
        m_server->world().setBlock(wall, qcAirBlockId);
        ticks(1);
        QVERIFY(!projectiles().all().front().stuck);
        ticks(20);
        QVERIFY(projectiles().all().front().stuck);
        QCOMPARE(projectiles().all().front().stuckIn.y, m_feet.y - 1);

        // It goes after despawnSeconds.
        ticks(int(std::lround((arrow().despawnSeconds - projectiles().all().front().age) / dt)) - 2);
        QCOMPARE(projectiles().all().size(), 1);
        ticks(3);
        QVERIFY(projectiles().all().isEmpty());
    }

    void playersPickUpStuckArrows()
    {
        join();
        const int before = player().inventory.countOf(item("qtcraft:arrow"));
        // Into the ground right beside the player, not through him.
        const QcVec3 beside{m_feet.x + 1.2, m_feet.y + 3.0, m_feet.z + 0.5};
        QVERIFY(shoot(beside, {0.0, -30.0, 0.0}));
        ticks(3);
        QVERIFY(projectiles().all().isEmpty());
        QCOMPARE(player().inventory.countOf(item("qtcraft:arrow")), before + 1);

        // Shot in creative mode: it goes, no arrow comes back.
        QVERIFY(shoot(beside, {0.0, -30.0, 0.0}, QcProjectilePickup::Remove));
        ticks(3);
        QVERIFY(projectiles().all().isEmpty());
        QCOMPARE(player().inventory.countOf(item("qtcraft:arrow")), before + 1);
    }

    void aBowShootsByTheDrawAndUsesArrows()
    {
        join();
        give("qtcraft:bow", 1);
        give("qtcraft:arrow", 3);
        const int bowSlot = slotHolding(item("qtcraft:bow"));
        player().inventory.setSelectedHotbarSlot(bowSlot);
        player().yaw = 90.0f;
        player().pitch = 10.0f;

        // A draw of one tick is too weak.
        send(QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Draw});
        send(QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Release});
        QVERIFY(projectiles().all().isEmpty());
        QCOMPARE(player().inventory.countOf(item("qtcraft:arrow")), 3);

        // A full draw: one arrow at 60 m/s along the view, the bow wears by one; others see the aim.
        send(QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Draw});
        QVERIFY(player().drawing);
        ticks(25);
        send(QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Release});
        QVERIFY(!player().drawing);
        QCOMPARE(projectiles().all().size(), 1);
        const QcProjectile &shot = projectiles().all().front();
        QCOMPARE(shot.shooter, player().id);
        QVERIFY(shot.motion.velocity.x > 50.0 && shot.motion.velocity.y > 0.0);
        QCOMPARE(player().inventory.countOf(item("qtcraft:arrow")), 2);
        QCOMPARE(player().inventory.slot(bowSlot).damage, 1);

        // Changing the slot lets the draw go.
        send(QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Draw});
        player().inventory.setSelectedHotbarSlot((bowSlot + 1) % 9);
        ticks(25);
        QVERIFY(!player().drawing);
        player().inventory.setSelectedHotbarSlot(bowSlot);
        send(QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Release});
        QCOMPARE(projectiles().all().size(), 1);

        // In creative mode it shoots without arrows; its arrow is only removed when picked up.
        player().gameMode = QcGameMode::Creative;
        player().inventory.setSlot(slotHolding(item("qtcraft:arrow")), {});
        QVERIFY(m_server->systems().projectiles.launchers().shoot(player(), 1.0));
        QCOMPARE(projectiles().all().last().pickup, QcProjectilePickup::Remove);
        QCOMPARE(player().inventory.slot(bowSlot).damage, 1);
    }

    void drawsEndWithoutAShot()
    {
        using Action = QcMsg::DrawLauncher::Action;
        join();
        give("qtcraft:bow", 1);
        const int bowSlot = slotHolding(item("qtcraft:bow"));
        player().inventory.setSelectedHotbarSlot(bowSlot);

        // Without ammunition no draw begins in survival mode.
        send(QcMsg::DrawLauncher{Action::Draw});
        QVERIFY(!player().drawing);

        // From one bow to another in the next slot: the draw belongs to its slot and goes.
        const int otherBow = (bowSlot + 1) % QcInventory::hotbarSize;
        player().inventory.setSlot(otherBow, {item("qtcraft:bow"), 1});
        give("qtcraft:arrow", 3);
        send(QcMsg::DrawLauncher{Action::Draw});
        QVERIFY(player().drawing);
        ticks(25);
        player().inventory.setSelectedHotbarSlot(otherBow);
        ticks(1);
        QVERIFY(!player().drawing);
        send(QcMsg::DrawLauncher{Action::Release});
        QVERIFY(projectiles().all().isEmpty());

        // A cancel (a screen opened) lets a full draw go.
        send(QcMsg::DrawLauncher{Action::Draw});
        ticks(25);
        send(QcMsg::DrawLauncher{Action::Cancel});
        QVERIFY(!player().drawing);
        send(QcMsg::DrawLauncher{Action::Release});
        QVERIFY(projectiles().all().isEmpty());

        // Death ends the draw at once; a release after the respawn shoots nothing.
        send(QcMsg::DrawLauncher{Action::Draw});
        ticks(25);
        chat(u"/kill"_s);
        QVERIFY(!player().drawing);
        send(QcMsg::Respawn{});
        send(QcMsg::DrawLauncher{Action::Release});
        QVERIFY(projectiles().all().isEmpty());
    }

    void theClientDrawsByTheSameRules()
    {
        // The client's draw (the animation) follows the server: none without ammunition outside
        // creative mode, and it belongs to the slot it began in.
        QcInventory inventory;
        inventory.setSlot(0, {item("qtcraft:bow"), 1});
        inventory.setSlot(1, {item("qtcraft:bow"), 1});
        const QcItemDefinition &bow = m_content->item(item("qtcraft:bow"));
        QcClientLauncher launcher;
        QVERIFY(!launcher.press(bow, inventory, *m_content, false));
        QVERIFY(launcher.press(bow, inventory, *m_content, true));
        QVERIFY(launcher.release());

        inventory.setSlot(5, {item("qtcraft:arrow"), 4});
        QVERIFY(launcher.press(bow, inventory, *m_content, false));
        launcher.tick(&bow, 0);
        QVERIFY(launcher.drawing());
        launcher.tick(&bow, 1);
        QVERIFY(!launcher.drawing());
        QVERIFY(!launcher.release());
    }

    void unknownProjectilesAreNoAmmunition()
    {
        QcContentRegistry content;
        QcProjectileDefinition stone;
        stone.name = u"test:stone"_s;
        stone.item = u"test:pebble"_s;
        content.registerProjectile(stone);
        QcProjectileDefinition orphan;
        orphan.name = u"test:orphan"_s;
        orphan.item = u"test:nothing"_s;
        content.registerProjectile(orphan);
        QcItemDefinition dart;
        dart.name = u"test:dart"_s;
        dart.projectile = u"test:missing"_s;
        QcItemDefinition pebble;
        pebble.name = u"test:pebble"_s;
        pebble.projectile = u"test:stone"_s;
        const QcResult<QcItemId> dartId = content.registerItem(dart);
        const QcResult<QcItemId> pebbleId = content.registerItem(pebble);
        QVERIFY(dartId && pebbleId);

        // Content loading warns about both broken names.
        const QStringList warnings = content.finish();
        QVERIFY2(warnings.contains(u"item 'test:dart': no projectile 'test:missing'; it is no ammunition"_s), qPrintable(warnings.join(u'\n')));
        QVERIFY2(warnings.contains(u"projectile 'test:orphan': no item 'test:nothing'"_s), qPrintable(warnings.join(u'\n')));

        // A bow passes over the dart to the pebble.
        QcInventory inventory;
        inventory.setSlot(0, {*dartId, 1});
        QCOMPARE(inventory.ammunitionSlot(content, QcLauncherDefinition{}), -1);
        inventory.setSlot(7, {*pebbleId, 1});
        QCOMPARE(inventory.ammunitionSlot(content, QcLauncherDefinition{}), 7);
    }

    void boundsInTheWorldAndInAChunk()
    {
        QcServerConfig config;
        config.limits.projectiles = 16;
        config.limits.projectilesPerChunk = 3;
        join(config);
        const QcBlockPos wall = wallAt(3);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(shoot({m_feet.x + 1.5, m_feet.y + 1.5, m_feet.z + 0.5}, {30.0, 0.0, 0.0}, QcProjectilePickup::Never));
            ticks(3);
        }
        QCOMPARE(projectiles().all().size(), 3);
        QCOMPARE(projectiles().crowdedOut(), 2);
        QVERIFY(std::ranges::all_of(projectiles().all(), [&](const QcProjectile &p) { return p.stuck && p.stuckIn == wall; }));

        for (int i = 0; i < 13; ++i) {
            QVERIFY(shoot({m_feet.x + 0.5, m_feet.y + 30.5, m_feet.z + 0.5}, {0.0, 5.0, 0.0}));
        }
        QVERIFY(!shoot({m_feet.x + 0.5, m_feet.y + 30.5, m_feet.z + 0.5}, {0.0, 5.0, 0.0}));
        QCOMPARE(projectiles().all().size(), 16);
    }

    void theVoidTakesThem()
    {
        join();
        QVERIFY(shoot({m_feet.x + 0.5, QcWorld::voidLimitY + 1.0, m_feet.z + 0.5}, {0.0, -60.0, 0.0}));
        ticks(2);
        QVERIFY(projectiles().all().isEmpty());
    }

    void savedWithTheirChunk()
    {
        join();
        wallAt(3);
        QVERIFY(shoot({m_feet.x + 1.5, m_feet.y + 1.5, m_feet.z + 0.5}, {30.0, 0.0, 0.0}, QcProjectilePickup::Never));
        QVERIFY(shoot({m_feet.x + 0.5, m_feet.y + 20.5, m_feet.z + 0.5}, {0.0, 10.0, 1.0}));
        ticks(3);
        QCOMPARE(projectiles().all().size(), 2);

        // Through the codec: every field comes back.
        QcEntityRecord record;
        record.chunk = m_feet.chunk();
        for (const QcProjectile &shot : projectiles().all()) {
            record.projectiles.append({shot, shot.id});
        }
        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> back =
            QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), record.chunk, *m_content, warnings);
        QVERIFY(back.has_value());
        QCOMPARE(back->projectiles.size(), 2);
        for (qsizetype i = 0; i < 2; ++i) {
            const QcProjectile &a = record.projectiles[i].entry;
            const QcProjectile &b = back->projectiles[i].entry;
            QCOMPARE(b.type, a.type);
            QCOMPARE(b.motion.position, a.motion.position);
            QCOMPARE(b.motion.velocity, a.motion.velocity);
            QCOMPARE(b.stuck, a.stuck);
            QCOMPARE(b.stuckIn, a.stuckIn);
            QCOMPARE(b.stuckState, a.stuckState);
            QCOMPARE(b.pickup, a.pickup);
            QCOMPARE(b.age, a.age);
            QCOMPARE(b.pitch, a.pitch);
        }

        // Unloaded with the chunk and back: the stuck one still sticks, the flying one flies on.
        const QHash<QcChunkPos, QcEntityRecord> taken = QcEntityRecords::take(m_server->entities(), {m_feet.chunk()});
        QVERIFY(projectiles().all().isEmpty());
        QVERIFY(QcEntityRecords::apply(taken.value(m_feet.chunk()), m_server->entities()).isEmpty());
        QCOMPARE(projectiles().all().size(), 2);
        QCOMPARE(std::ranges::count_if(projectiles().all(), &QcProjectile::stuck), 1);
    }
};

QTEST_GUILESS_MAIN(TestProjectiles)
#include "test_projectiles.moc"
