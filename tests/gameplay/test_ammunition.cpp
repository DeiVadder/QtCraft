#include "clientcore/session/QcClientLauncher.h"
#include "common/content/parser/QcContentParser.h"
#include "common/content/parser/QcProjectileParser.h"
#include "common/content/registry/QcContentSerializer.h"
#include "QcServerTestFixture.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/projectiles/QcHitEffects.h"
#include "server/projectiles/QcPlayerLaunchers.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

// Ammunition in general (decision E-427): what each launcher takes, the hit effects of projectile
// kinds from data (damage, knockback, ignite, break; unknown types left out), throwing by hand with
// its cooldown, the snowball, and a mod's second kind of arrow. Ticks are stepped by hand.
class TestAmmunition : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }
    const QcServerClient &constPlayer() const { return *m_server->entities().clients().front(); }
    QcProjectileManager &projectiles() { return m_server->entities().projectiles(); }

    void give(const char *name, int count) { player().inventory.add(item(name), count, *m_content); }

    // Selects the first slot holding `name`.
    void select(const char *name)
    {
        for (int slot = 0; slot < 9; ++slot) {
            if (player().inventory.slot(slot).item == item(name)) {
                player().inventory.setSelectedHotbarSlot(slot);
                return;
            }
        }
        QFAIL("not in the hotbar");
    }

    // A pig 3.5 blocks east of the player and a projectile of `kind` flying at it from the west.
    quint32 pigUnderFire(const QString &kind, double speed)
    {
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const quint32 mob = m_server->entities().creatures().spawn(pig, {m_feet.x + 3.5, static_cast<double>(m_feet.y), m_feet.z - 2.5},
                                                                   m_server->entities().newId());
        m_server->systems().projectiles.launch({0, kind, {m_feet.x - 1.5, m_feet.y + 0.5, m_feet.z - 2.5}, {speed, 0.0, 0.0}});
        return mob;
    }

    // The base content with `kind` added (a mod's projectile), and its `item` with `tags`.
    void addKind(const QcProjectileDefinition &kind, const QStringList &tags)
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        content->registerProjectile(kind);
        QcItemDefinition ammunition;
        ammunition.name = kind.item;
        ammunition.projectile = kind.name;
        ammunition.tags = tags;
        QVERIFY(content->registerItem(ammunition).has_value());
        m_content = content;
    }

    static QcProjectileDefinition parsed(const char *json)
    {
        const QcResult<QcProjectileDefinition> kind = QcProjectileParser::parseProjectile(QJsonDocument::fromJson(json).object(), u"test"_s);
        return kind ? *kind : QcProjectileDefinition{};
    }

    // Snowballs on the ground and in the player's inventory.
    int snowballs() const
    {
        int total = constPlayer().inventory.countOf(item("qtcraft:snowball"));
        for (const QcItemEntity &entity : m_server->items().items()) {
            total += entity.stack.item == item("qtcraft:snowball") ? entity.stack.count : 0;
        }
        return total;
    }

    // Mines `name` placed beside the player's feet with the selected item; the snowballs it gave.
    int snowballsFromMining(const char *name)
    {
        const QcBlockPos target = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(target, id(name));
        const int before = snowballs();
        send(QcMsg::StartMining{target});
        ticks(80);
        send(QcMsg::FinishMining{target});
        ticks(2);
        if (m_server->world().blockAt(target) != qcAirBlockId) {
            return -1;
        }
        return snowballs() - before;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void theArrowKeepsItsHit()
    {
        // The arrow's file names damage 2 and knockback 1; a file without onHit means the same.
        const QcProjectileDefinition &arrow = *m_content->projectile(u"qtcraft:arrow"_s);
        QCOMPARE(arrow.onHit.size(), 2);
        QVERIFY(arrow.onHit[0].is(QcHitEffect::damage) && arrow.onHit[1].is(QcHitEffect::knockback));
        QCOMPARE(arrow.damage, 2.0);
        QVERIFY(!arrow.breaks());
        const QcProjectileDefinition plain = parsed(R"({"name": "dart", "item": "dart", "damage": 3})");
        QCOMPARE(plain.onHit.size(), 2);
        QCOMPARE(plain.damage, 3.0);
        QCOMPARE(plain.onHit[1].number, 1.0);

        const QcHitEffects::Impact impact = QcHitEffects().impactOf(arrow, 6.0);
        QCOMPARE(impact.damage, 6.0);
        QCOMPARE(impact.knockback, 1.0);
        QCOMPARE(impact.burnTicks, 0);
    }

    void aDamageEntryWithoutAmountDealsTheKindsDamage()
    {
        const QcProjectileDefinition dart = parsed(R"({"name": "dart", "item": "dart", "damage": 5,
            "onHit": [{"type": "damage"}, {"type": "knockback", "strength": 0.5}]})");
        QCOMPARE(dart.onHit.size(), 2);
        QCOMPARE(dart.onHit[0].number, 5.0);
        QCOMPARE(dart.damage, 5.0);
        // An amount of its own wins; without `damage` the default of a kind.
        QCOMPARE(parsed(R"({"name": "a", "item": "a", "damage": 5, "onHit": [{"type": "damage", "amount": 3}]})").damage, 3.0);
        QCOMPARE(parsed(R"({"name": "a", "item": "a", "onHit": [{"type": "damage"}]})").damage, QcProjectileDefinition{}.damage);
    }

    void knockbackStrengthScalesOnlyThePush()
    {
        // At strength 1 a blow's push; the lift never grows with the strength.
        const QcVec3 east{1.0, 0.0, 0.0};
        QCOMPARE(QcMobManager::knockbackVelocity(east, 1.0), (QcVec3{QcMobManager::knockbackSpeed, QcMobManager::knockbackLift, 0.0}));
        QCOMPARE(QcMobManager::knockbackVelocity(east, 0.5),
                 (QcVec3{QcMobManager::knockbackSpeed * 0.5, QcMobManager::knockbackLift * 0.5, 0.0}));
        QCOMPARE(QcMobManager::knockbackVelocity(east, 4.0), (QcVec3{QcMobManager::knockbackSpeed * 4.0, QcMobManager::knockbackLift, 0.0}));

        // Applied to bodies: a pig hit from the west and the player.
        join();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        QcMobManager &creatures = m_server->entities().creatures();
        for (const double strength : {0.5, 4.0}) {
            const QcVec3 at{m_feet.x + 3.5, static_cast<double>(m_feet.y), m_feet.z - 2.5};
            const quint32 mob = creatures.spawn(pig, at, m_server->entities().newId());
            QVERIFY(!creatures.damage({.creature = mob, .damage = 0.0, .from = at - QcVec3{1.0, 0.0, 0.0}, .knockback = strength}));
            const QcVec3 pushed = creatures.find(mob)->motion.velocity;
            QVERIFY(std::abs(pushed.x - QcMobManager::knockbackSpeed * strength) < 1e-9);
            QVERIFY(std::abs(pushed.y - QcMobManager::knockbackLift * std::min(strength, 1.0)) < 1e-9);
        }
        QcPlayerHealth::knock(player(), {0.0, 0.0, -2.0}, 4.0);
        QVERIFY(std::abs(player().motion.velocity.z + QcMobManager::knockbackSpeed * 4.0) < 1e-9);
        QCOMPARE(player().motion.velocity.y, QcMobManager::knockbackLift);
    }

    void unknownEffectTypesAreLeftOut()
    {
        QcContentRegistry content;
        QcProjectileDefinition kind = parsed(R"({"name": "bomb", "item": "bomb",
            "onHit": [{"type": "explode", "power": 4}, {"type": "knockback", "strength": 0.5}]})");
        QCOMPARE(kind.onHit.size(), 2);
        QCOMPARE(kind.onHit[0].fields.value(u"power"_s).toInt(), 4);
        content.registerProjectile(kind);
        QcItemDefinition bomb;
        bomb.name = u"test:bomb"_s;
        bomb.projectile = u"test:bomb"_s;
        QVERIFY(content.registerItem(bomb).has_value());

        const QStringList warnings = content.finish();
        QVERIFY2(warnings.contains(u"projectile 'test:bomb': unknown onHit type 'explode' left out"_s), qPrintable(warnings.join(u'\n')));
        const QcProjectileDefinition &left = *content.projectile(u"test:bomb"_s);
        QCOMPARE(left.onHit.size(), 1);
        QCOMPARE(left.onHit[0].number, 0.5);
        QCOMPARE(left.damage, 0.0);

        // A number out of its range and an entry without a type are errors of the file.
        QVERIFY(!QcProjectileParser::parseProjectile(QJsonDocument::fromJson(R"({"name": "a", "item": "a",
            "onHit": [{"type": "ignite", "seconds": -1}]})").object(), u"test"_s));
        QVERIFY(!QcProjectileParser::parseProjectile(QJsonDocument::fromJson(R"({"name": "a", "item": "a",
            "onHit": [{"strength": 1}]})").object(), u"test"_s));
    }

    void theBowTakesOnlyArrows()
    {
        const QcItemDefinition &bow = m_content->item(item("qtcraft:bow"));
        QCOMPARE(bow.launcher->ammunition, QStringList{u"#qtcraft:arrows"_s});

        QcInventory inventory;
        inventory.setSlot(0, {item("qtcraft:snowball"), 4});
        QCOMPARE(inventory.ammunitionSlot(*m_content, *bow.launcher), -1);
        // A launcher without a list takes no throwable items either.
        QCOMPARE(inventory.ammunitionSlot(*m_content, QcLauncherDefinition{}), -1);
        inventory.setSlot(5, {item("qtcraft:arrow"), 2});
        QCOMPARE(inventory.ammunitionSlot(*m_content, *bow.launcher), 5);

        // On the client too, a bow with snowballs only does not draw.
        QcInventory snowOnly;
        snowOnly.setSlot(0, {item("qtcraft:bow"), 1});
        snowOnly.setSlot(1, {item("qtcraft:snowball"), 4});
        QcClientLauncher launcher;
        QVERIFY(!launcher.press(bow, snowOnly, *m_content, false));
    }

    void aBowPassesOverSnowballs()
    {
        join();
        give("qtcraft:snowball", 8);
        give("qtcraft:bow", 1);
        select("qtcraft:bow");

        // Snowballs only: no draw, nothing shot.
        send(QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Draw});
        QVERIFY(!player().drawing);
        QVERIFY(!m_server->systems().projectiles.launchers().shoot(player(), 1.0));

        // With an arrow behind the snowballs the arrow goes.
        give("qtcraft:arrow", 1);
        QVERIFY(m_server->systems().projectiles.launchers().shoot(player(), 1.0));
        QCOMPARE(projectiles().all().last().type, u"qtcraft:arrow"_s);
        QCOMPARE(player().inventory.countOf(item("qtcraft:snowball")), 8);
        QCOMPARE(player().inventory.countOf(item("qtcraft:arrow")), 0);

        // Creative mode without arrows shoots the first kind the bow takes, never a snowball.
        player().gameMode = QcGameMode::Creative;
        QVERIFY(m_server->systems().projectiles.launchers().shoot(player(), 1.0));
        QCOMPARE(projectiles().all().last().type, u"qtcraft:arrow"_s);
    }

    void creativeShootsTheFirstKindTheLauncherTakes()
    {
        // A throwable kind and a second kind of the arrow item both come before the arrow.
        QcContentRegistry content;
        const auto addItem = [&content](const QString &name, const QString &projectile, const QStringList &tags, bool throwable) {
            QcItemDefinition definition;
            definition.name = name;
            definition.projectile = projectile;
            definition.tags = tags;
            if (throwable) {
                definition.throwable = QcThrowableDefinition{};
            }
            QVERIFY(content.registerItem(definition).has_value());
        };
        addItem(u"test:ball"_s, u"test:ball"_s, {}, true);
        addItem(u"qtcraft:arrow"_s, u"qtcraft:arrow"_s, {u"qtcraft:arrows"_s}, false);
        for (const auto &[kind, of] : {std::pair{u"test:ball"_s, u"test:ball"_s}, std::pair{u"test:alias"_s, u"qtcraft:arrow"_s},
                                       std::pair{u"qtcraft:arrow"_s, u"qtcraft:arrow"_s}}) {
            QcProjectileDefinition projectile;
            projectile.name = kind;
            projectile.item = of;
            content.registerProjectile(projectile);
        }
        QCOMPARE(content.projectiles().first().name, u"test:ball"_s);

        // The bow passes over the ball and shoots the arrow item's own kind, not the alias.
        const QcLauncherDefinition &bow = *m_content->item(item("qtcraft:bow")).launcher;
        QCOMPARE(QcPlayerLaunchers::creativeProjectile(content, bow), u"qtcraft:arrow"_s);
        // A launcher without a list takes no throwable item either.
        QCOMPARE(QcPlayerLaunchers::creativeProjectile(content, QcLauncherDefinition{}), u"qtcraft:arrow"_s);
        QcLauncherDefinition balls;
        balls.ammunition = {u"test:ball"_s};
        QCOMPARE(QcPlayerLaunchers::creativeProjectile(content, balls), u"test:ball"_s);
        balls.ammunition = {u"test:nothing"_s};
        QVERIFY(QcPlayerLaunchers::creativeProjectile(content, balls).isEmpty());
    }

    void contradictoryUsesAreLeftOutWithAWarning()
    {
        QcContentRegistry content;
        QcProjectileDefinition ball;
        ball.name = u"test:hot_ball"_s;
        ball.item = u"test:hot_ball"_s;
        content.registerProjectile(ball);
        QcItemDefinition flint;
        flint.name = u"test:hot_ball"_s;
        flint.projectile = u"test:hot_ball"_s;
        flint.throwable = QcThrowableDefinition{};
        flint.ignites = true;
        QVERIFY(content.registerItem(flint).has_value());
        QcItemDefinition sling;
        sling.name = u"test:sling"_s;
        sling.launcher = QcLauncherDefinition{};
        sling.launcher->emptyAmmunitionList = true;
        QVERIFY(content.registerItem(sling).has_value());

        const QStringList warnings = content.finish();
        QVERIFY2(warnings.contains(u"item 'test:hot_ball': a 'throwable' item neither ignites, fertilizes, scoops nor bottles; it is only thrown"_s),
                 qPrintable(warnings.join(u'\n')));
        QVERIFY(content.item(*content.itemId(u"test:hot_ball"_s)).throwable);
        QVERIFY(!content.item(*content.itemId(u"test:hot_ball"_s)).ignites);
        QVERIFY2(warnings.contains(u"item 'test:sling': an empty 'ammunition' list means none: the launcher takes all ammunition that is not thrown"_s),
                 qPrintable(warnings.join(u'\n')));
        QVERIFY(!content.item(*content.itemId(u"test:sling"_s)).launcher->emptyAmmunitionList);

        // The parser marks the empty list.
        const QcResult<QcItemDefinition> parsedSling = QcContentParser::parseItem(
            QJsonDocument::fromJson(R"({"name": "sling", "launcher": {"ammunition": []}})").object(), u"test"_s);
        QVERIFY(parsedSling && parsedSling->launcher && parsedSling->launcher->emptyAmmunitionList);
    }

    void snowballsAreThrownWithACooldown()
    {
        join();
        give("qtcraft:snowball", 4);
        select("qtcraft:snowball");
        player().pitch = 45.0f;
        const QcThrowableDefinition throwable = *m_content->item(item("qtcraft:snowball")).throwable;
        const int cooldown = static_cast<int>(std::lround(throwable.cooldownSeconds / QcPhysics::tickDuration));
        QVERIFY(cooldown >= 2);

        send(QcMsg::ThrowItem{});
        QCOMPARE(player().inventory.countOf(item("qtcraft:snowball")), 3);
        QCOMPARE(projectiles().all().size(), 1);
        const QcProjectile &ball = projectiles().all().front();
        QCOMPARE(ball.type, u"qtcraft:snowball"_s);
        QCOMPARE(ball.shooter, player().id);
        QVERIFY(std::abs(ball.motion.velocity.length() - throwable.speed) < 1.0);

        // Within the cooldown nothing goes, also in its last tick; after it the next one does. send()
        // runs one tick, so the throw below is handled cooldown - 1 ticks after the first.
        ticks(cooldown - 2);
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().inventory.countOf(item("qtcraft:snowball")), 3);
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().inventory.countOf(item("qtcraft:snowball")), 2);

        // Creative mode throws without using any.
        player().gameMode = QcGameMode::Creative;
        ticks(cooldown);
        send(QcMsg::ThrowItem{});
        QCOMPARE(player().inventory.countOf(item("qtcraft:snowball")), 2);
        QCOMPARE(projectiles().all().last().pickup, QcProjectilePickup::Remove);

        // Other items are not thrown.
        give("qtcraft:arrow", 1);
        select("qtcraft:arrow");
        const qsizetype before = projectiles().all().size();
        QVERIFY(!m_server->systems().projectiles.throws().throwHeld(player()));
        QCOMPARE(projectiles().all().size(), before);
    }

    void theClientKeepsTheCooldown()
    {
        // 0.25 s: 5 server ticks.
        const QcThrowableDefinition throwable{30.0, 0.25, 1.0};
        QcClientLauncher launcher;
        QVERIFY(launcher.mayThrow(7, throwable));
        QVERIFY(!launcher.mayThrow(7, throwable));
        // Each item has its own cooldown.
        QVERIFY(launcher.mayThrow(8, throwable));
        for (int tick = 0; tick < 4; ++tick) {
            launcher.tick(nullptr, 0);
        }
        QVERIFY(!launcher.mayThrow(7, throwable));
        launcher.tick(nullptr, 0);
        QVERIFY(launcher.mayThrow(7, throwable));

        // At twice the server's tick rate the cooldown passes in half the client ticks, frozen never.
        launcher.tick(nullptr, 0, 2.0);
        launcher.tick(nullptr, 0, 2.0);
        QVERIFY(!launcher.mayThrow(7, throwable));
        launcher.tick(nullptr, 0, 2.0);
        QVERIFY(launcher.mayThrow(7, throwable));
        for (int tick = 0; tick < 10; ++tick) {
            launcher.tick(nullptr, 0, 0.0);
        }
        QVERIFY(!launcher.mayThrow(7, throwable));
    }

    void theClientsUseButtonThrowsOrDraws()
    {
        // The hand sends what use() gives and a throw swings the arm (QcHandActions::use, applied by
        // QcClientSession::useItem).
        QcInventory inventory;
        inventory.setSlot(0, {item("qtcraft:snowball"), 4});
        inventory.setSlot(1, {item("qtcraft:bow"), 1});
        inventory.setSlot(2, {item("qtcraft:arrow"), 4});
        const QcItemDefinition &snowball = m_content->item(item("qtcraft:snowball"));
        const QcItemDefinition &bow = m_content->item(item("qtcraft:bow"));
        QVERIFY(QcClientLauncher::uses(snowball) && QcClientLauncher::uses(bow));
        QVERIFY(!QcClientLauncher::uses(m_content->item(item("qtcraft:arrow"))));

        QcClientLauncher launcher;
        const std::optional<QcMessage> thrown = launcher.use(item("qtcraft:snowball"), snowball, inventory, *m_content, false);
        QVERIFY(thrown && std::holds_alternative<QcMsg::ThrowItem>(*thrown));
        QCOMPARE(QcClientLauncher::swingAfter(thrown, 4), 0);
        // Within the cooldown nothing is sent and the swing goes on.
        const std::optional<QcMessage> again = launcher.use(item("qtcraft:snowball"), snowball, inventory, *m_content, false);
        QVERIFY(!again);
        QCOMPARE(QcClientLauncher::swingAfter(again, 4), 4);

        inventory.setSelectedHotbarSlot(1);
        const std::optional<QcMessage> drawn = launcher.use(item("qtcraft:bow"), bow, inventory, *m_content, false);
        QVERIFY(drawn && std::holds_alternative<QcMsg::DrawLauncher>(*drawn));
        QCOMPARE(std::get<QcMsg::DrawLauncher>(*drawn).action, QcMsg::DrawLauncher::Action::Draw);
        QCOMPARE(QcClientLauncher::swingAfter(drawn, 4), 4);
        QVERIFY(launcher.drawing());
    }

    void snowballsPushWithoutHarmAndBreak()
    {
        join();
        const quint32 mob = pigUnderFire(u"qtcraft:snowball"_s, 30.0);
        const double health = m_server->entities().creatures().find(mob)->health;
        // 5 m at 1.5 m per tick.
        ticks(4);
        const QcCreature *pig = m_server->entities().creatures().find(mob);
        QVERIFY(pig);
        QCOMPARE(pig->health, health);
        QVERIFY(pig->motion.velocity.x > 0.0);
        QVERIFY(projectiles().all().isEmpty());
        // A hit without damage gives no invulnerability: an arrow right after it still hurts.
        QVERIFY(!pig->hurt.active());
        const QcVec3 pushed = pig->motion.position;
        m_server->systems().projectiles.launch({0, u"qtcraft:arrow"_s, {pushed.x - 1.0, pushed.y + 0.5, pushed.z}, {30.0, 0.0, 0.0}});
        ticks(1);
        QVERIFY(projectiles().all().isEmpty());
        QVERIFY(m_server->entities().creatures().find(mob)->health < health);

        // During the pig's invulnerability after the arrow a snowball breaks too instead of bouncing.
        QVERIFY(m_server->entities().creatures().find(mob)->hurt.active());
        const QcVec3 hurt = m_server->entities().creatures().find(mob)->motion.position;
        m_server->systems().projectiles.launch({0, u"qtcraft:snowball"_s, {hurt.x - 1.0, hurt.y + 0.5, hurt.z}, {30.0, 0.0, 0.0}});
        ticks(1);
        QVERIFY(projectiles().all().isEmpty());

        // A block breaks it instead of holding it.
        const QcBlockPos wall = m_feet.offset(3, 1, 0);
        m_server->world().setBlock(wall, id("qtcraft:stone"));
        m_server->systems().projectiles.launch({0, u"qtcraft:snowball"_s, {m_feet.x + 0.5, wall.y + 0.5, m_feet.z + 0.5}, {30.0, 0.0, 0.0}});
        ticks(3);
        QVERIFY(projectiles().all().isEmpty());

        // A player is pushed and keeps his health.
        const double before = player().health;
        const QcVec3 at = player().motion.position;
        m_server->systems().projectiles.launch({0, u"qtcraft:snowball"_s, {at.x - 2.0, at.y + 1.0, at.z}, {30.0, 0.0, 0.0}});
        ticks(1);
        QCOMPARE(player().health, before);
        QVERIFY(player().motion.velocity.x > 0.0);
        QVERIFY(!player().hurt.active());
    }

    void igniteSetsABodyBurning()
    {
        QcProjectileDefinition fire = parsed(R"({"name": "fire_dart", "item": "fire_dart", "sticks": false,
            "onHit": [{"type": "ignite", "seconds": 4}]})");
        QCOMPARE(fire.onHit.size(), 1);
        addKind(fire, {});
        join();

        const quint32 mob = pigUnderFire(u"test:fire_dart"_s, 30.0);
        const double health = m_server->entities().creatures().find(mob)->health;
        // 5 m at 1.5 m per tick.
        ticks(4);
        const QcCreature *pig = m_server->entities().creatures().find(mob);
        QVERIFY(pig);
        QVERIFY2(pig->burnTicks >= 78, qPrintable(QString::number(pig->burnTicks)));
        // No damage effect: the hit itself does not hurt; no knockback effect: no push.
        QVERIFY(pig->health >= health - 1.0);
        QVERIFY(std::abs(pig->motion.velocity.x) < QcMobManager::knockbackSpeed * 0.5);

        const QcVec3 at = player().motion.position;
        m_server->systems().projectiles.launch({0, u"test:fire_dart"_s, {at.x - 2.0, at.y + 1.0, at.z}, {30.0, 0.0, 0.0}});
        QVERIFY(!m_client->burning);
        ticks(1);
        QVERIFY(player().burnTicks >= 78);
        // The hit does no damage, yet the client learns at once that the player burns.
        QVERIFY(m_client->burning);
    }

    void aModsSecondArrowFitsTheBow()
    {
        QcProjectileDefinition flaming = parsed(R"({"name": "flaming_arrow", "item": "flaming_arrow",
            "onHit": [{"type": "damage", "amount": 2}, {"type": "knockback"}, {"type": "ignite", "seconds": 5}]})");
        addKind(flaming, {u"qtcraft:arrows"_s});

        const QcItemDefinition &bow = m_content->item(item("qtcraft:bow"));
        const QcItemId ammunition = *m_content->itemId(u"test:flaming_arrow"_s);
        QcInventory inventory;
        inventory.setSlot(3, {ammunition, 5});
        QCOMPARE(inventory.ammunitionSlot(*m_content, *bow.launcher), 3);

        // A launcher naming only the flaming arrow passes over plain arrows.
        QcLauncherDefinition only;
        only.ammunition = {u"test:flaming_arrow"_s};
        inventory.setSlot(0, {item("qtcraft:arrow"), 5});
        QCOMPARE(inventory.ammunitionSlot(*m_content, only), 3);
        QCOMPARE(inventory.ammunitionSlot(*m_content, *bow.launcher), 0);

        const QcHitEffects::Impact impact = QcHitEffects().impactOf(*m_content->projectile(u"test:flaming_arrow"_s), 6.0);
        QCOMPARE(impact.damage, 6.0);
        QCOMPARE(impact.burnTicks, 100);
    }

    void theClientLearnsAmmunitionAndThrowing()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> client = QcContentSerializer::read(in);
        QVERIFY(client.has_value());
        const QcItemDefinition &bow = client->item(*client->itemId(u"qtcraft:bow"_s));
        QCOMPARE(bow.launcher->ammunition, QStringList{u"#qtcraft:arrows"_s});
        const QcItemDefinition &snowball = client->item(*client->itemId(u"qtcraft:snowball"_s));
        QVERIFY(snowball.throwable.has_value());
        QCOMPARE(*snowball.throwable, *m_content->item(item("qtcraft:snowball")).throwable);
    }

    void snowGivesSnowballsOnlyToAShovel()
    {
        join();
        // By hand neither a block nor a layer gives anything.
        QCOMPARE(snowballsFromMining("qtcraft:snow_block"), 0);
        QCOMPARE(snowballsFromMining("qtcraft:snow_layer"), 0);

        give("qtcraft:wooden_shovel", 1);
        select("qtcraft:wooden_shovel");
        QCOMPARE(snowballsFromMining("qtcraft:snow_block"), 4);
        QCOMPARE(snowballsFromMining("qtcraft:snow_layer"), 1);
    }
};

QTEST_GUILESS_MAIN(TestAmmunition)
#include "test_ammunition.moc"
