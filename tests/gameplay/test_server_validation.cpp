#include "QcHunger.h"
#include "QcMining.h"
#include "QcNameText.h"
#include "QcPhysics.h"
#include "QcProfileValidation.h"
#include "QcServerTestFixture.h"

#include <QTest>

using namespace Qt::StringLiterals;

// Checks that the server refuses what a client must not be able to do (mining, placing, inventory
// requests, commands, combat, food, appearances), with raw protocol messages.
class TestServerValidation : public QcServerTestFixture
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void miningNeedsTheFullTime()
    {
        join();
        const QcBlockPos below = m_feet.offset(0, -1, 0);
        QCOMPARE(m_server->world().blockAt(below), std::optional<QcBlockId>(id("qtcraft:grass_block")));

        // Grass: hardness 0.6 * 1.5 s = 0.9 s = 18 ticks.
        m_client->connection.send(QcMsg::StartMining{below});
        m_client->connection.send(QcMsg::FinishMining{below});
        pump();
        QCOMPARE(m_server->world().blockAt(below), std::optional<QcBlockId>(id("qtcraft:grass_block")));

        // 14 ticks are not enough even with the two ticks of slack.
        m_client->connection.send(QcMsg::StartMining{below});
        pump();
        ticks(14);
        m_client->connection.send(QcMsg::FinishMining{below});
        pump();
        QCOMPARE(m_server->world().blockAt(below), std::optional<QcBlockId>(id("qtcraft:grass_block")));

        m_client->connection.send(QcMsg::StartMining{below});
        pump();
        ticks(18);
        m_client->connection.send(QcMsg::FinishMining{below});
        pump();
        QCOMPARE(m_server->world().blockAt(below), std::optional<QcBlockId>(qcAirBlockId));

        // The drop lies in the world first and reaches the player once the pickup delay is over.
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        QCOMPARE(m_server->items().items().size(), 1);

        // The player falls into the hole and lands on the item.
        idle(int(QcItemEntityManager::pickupDelay * QcPhysics::tickRate) + 10);
        QCOMPARE(countOf(item("qtcraft:dirt")), 1);
        QVERIFY(m_server->items().items().isEmpty());
    }

    void miningOutOfReachIsRefused()
    {
        join();
        const QcBlockPos far = m_feet.offset(12, -1, 0);
        const std::optional<QcBlockId> before = m_server->world().blockAt(far);
        m_client->connection.send(QcMsg::StartMining{far});
        pump();
        ticks(40);
        m_client->connection.send(QcMsg::FinishMining{far});
        pump();
        QCOMPARE(m_server->world().blockAt(far), before);
    }

    void stoneByHandIsSlowAndDropsNothing()
    {
        join();
        const QcBlockPos target = m_feet.offset(1, 0, 0);
        QVERIFY(m_server->world().setBlock(target, id("qtcraft:stone")));
        const double seconds = QcMining::breakSeconds(m_content->block(id("qtcraft:stone")), nullptr);
        QCOMPARE(seconds, 7.5);

        m_client->connection.send(QcMsg::StartMining{target});
        pump();
        ticks(140);
        m_client->connection.send(QcMsg::FinishMining{target});
        pump();
        QCOMPARE(m_server->world().blockAt(target), std::optional<QcBlockId>(id("qtcraft:stone")));

        m_client->connection.send(QcMsg::StartMining{target});
        pump();
        ticks(150);
        m_client->connection.send(QcMsg::FinishMining{target});
        pump();
        QCOMPARE(m_server->world().blockAt(target), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(countOf(item("qtcraft:cobblestone")), 0);
    }

    void placingIsValidated()
    {
        join();
        const QcBlockPos ground = m_feet.offset(2, -1, 0);
        // No item in hand: nothing happens.
        m_client->connection.send(QcMsg::UseItem{ground, QcBlockFace::Up});
        pump();
        QCOMPARE(m_server->world().blockAt(ground.offset(0, 1, 0)), std::optional<QcBlockId>(qcAirBlockId));

        chat(u"/give bricks 2"_s);
        QCOMPARE(countOf(item("qtcraft:bricks")), 2);
        // Into the player's own feet: refused.
        m_client->connection.send(QcMsg::UseItem{m_feet.offset(0, -1, 0), QcBlockFace::Up});
        pump();
        QCOMPARE(m_server->world().blockAt(m_feet), std::optional<QcBlockId>(qcAirBlockId));
        // Out of reach: refused.
        m_client->connection.send(QcMsg::UseItem{m_feet.offset(10, -1, 0), QcBlockFace::Up});
        pump();
        QCOMPARE(countOf(item("qtcraft:bricks")), 2);
        // Next to the player: placed and consumed.
        m_client->connection.send(QcMsg::UseItem{ground, QcBlockFace::Up});
        pump();
        QCOMPARE(m_server->world().blockAt(ground.offset(0, 1, 0)), std::optional<QcBlockId>(id("qtcraft:bricks")));
        QCOMPARE(countOf(item("qtcraft:bricks")), 1);
    }

    void inventoryRequestsOutsideTheSlotsAreIgnored()
    {
        join();
        chat(u"/give glass 5"_s);
        const QList<QcItemStack> before = m_client->stacks;
        for (const auto &[from, to] : {std::pair{-1, 0}, std::pair{0, 400}, std::pair{-100, -100}}) {
            m_client->connection.send(QcMsg::MoveInventoryItem{from, to});
        }
        m_client->connection.send(QcMsg::SelectHotbarSlot{50});
        pump();
        QCOMPARE(m_client->stacks, before);
    }

    void cheatCommandsNeedAnOperator_data()
    {
        QTest::addColumn<QString>("command");
        QTest::newRow("give") << u"/give glass 1"_s;
        QTest::newRow("time") << u"/time set night"_s;
        QTest::newRow("summon") << u"/summon zombie"_s;
        QTest::newRow("kill") << u"/kill"_s;
        QTest::newRow("heal") << u"/heal"_s;
    }

    void cheatCommandsNeedAnOperator()
    {
        QFETCH(QString, command);
        join(notOperator());
        const double time = m_server->worldTime();
        chat(command);
        QVERIFY(m_client->chatLines.constLast().contains(u"Only operators"_s));
        QVERIFY(!m_client->dead);
        QCOMPARE(m_server->worldTime(), time);
        QCOMPARE(countOf(item("qtcraft:glass")), 0);
    }

    void deadPlayersCannotMineOrPlace()
    {
        join();
        chat(u"/give dirt 4"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), 4);
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);

        const QcBlockPos below = m_feet.offset(0, -1, 0);
        const QcBlockId before = *m_server->world().blockAt(below);
        m_client->connection.send(QcMsg::StartMining{below});
        ticks(200);
        m_client->connection.send(QcMsg::FinishMining{below});
        m_client->connection.send(QcMsg::UseItem{below.offset(2, 0, 0), QcBlockFace::Up});
        pump();
        ticks(2);

        QCOMPARE(*m_server->world().blockAt(below), before);
        QCOMPARE(*m_server->world().blockAt(m_feet.offset(2, 0, 0)), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:dirt")), 4);
    }

    void giveRejectsBadCountsAndNeedsAnOperator()
    {
        join();
        chat(u"/give glass -5"_s);
        chat(u"/give glass 999999"_s);
        chat(u"/give no_such_item"_s);
        QCOMPARE(countOf(item("qtcraft:glass")), 0);
        QCOMPARE(m_client->chatLines.size(), 3);
        cleanup();

        join(notOperator());
        chat(u"/give glass 1"_s);
        QCOMPARE(countOf(item("qtcraft:glass")), 0);
        QVERIFY(m_client->chatLines.constLast().contains(u"Only operators"_s));
    }

    void attacksNeedReachAndKillGivesLoot()
    {
        join();
        // A pig in a 1 x 1 pit two blocks deep right next to the player (who sees into it) cannot run away.
        const QcBlockPos pit = m_feet.offset(1, -1, 0);
        m_server->world().setBlock(pit, qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -1, 0), qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -2, 0), id("qtcraft:stone"));
        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s),
                                                    {pit.x + 0.5, double(pit.y - 1), pit.z + 0.5}, 500);
        ticks(1);
        QVERIFY(m_client->entities.contains(pig));
        // Bare hands deal 1 damage, the pig has 10 health; hits closer than 0.5 s apart do nothing.
        for (int attempt = 0; attempt < 15 && m_server->mobs().find(pig); ++attempt) {
            m_client->connection.send(QcMsg::AttackEntity{pig});
            pump();
            ticks(12);
        }
        QVERIFY(!m_server->mobs().find(pig));
        ticks(1);
        QVERIFY(m_client->died.contains(pig));
        // The loot falls into the pit, out of the player's reach.
        int dropped = 0;
        for (const QcItemEntity &loot : m_server->items().items()) {
            dropped += loot.stack.item == item("qtcraft:raw_porkchop") ? loot.stack.count : 0;
        }
        QCOMPARE(dropped, 2);
        QCOMPARE(countOf(item("qtcraft:raw_porkchop")), 0);

        // A mob far away cannot be hit.
        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), m_client->motion.position + QcVec3{20.0, 0.0, 0.0}, 999);
        m_client->connection.send(QcMsg::AttackEntity{999});
        pump();
        QCOMPARE(m_server->mobs().find(999)->health, 10.0);
    }

    void fallsHurtAndDeathNeedsARespawn()
    {
        join();
        QCOMPARE(m_client->health, 20.0);
        // Landing after 10 m deals floor(10 - 3) = 7 damage.
        fallDownShaft();
        QCOMPARE(m_client->motion.position.y, double(m_feet.y - 10));
        QCOMPARE(m_client->health, 13.0);

        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        QCOMPARE(m_client->health, 0.0);
        // A dead player does not move.
        const QcVec3 corpse = m_client->motion.position;
        QcPlayerInput walk;
        walk.sequence = 100;
        walk.forward = 1.0f;
        m_client->connection.send(QcMsg::PlayerInput{walk});
        pump();
        ticks(1);
        QCOMPARE(m_client->motion.position, corpse);

        // Refill the shaft: the spawn column's surface is back at the original height.
        for (int dy = -10; dy <= -1; ++dy) {
            m_server->world().setBlock(m_feet.offset(0, dy, 0), id("qtcraft:stone"));
        }
        m_client->connection.send(QcMsg::Respawn{});
        pump();
        ticks(1);
        QVERIFY(!m_client->dead);
        QCOMPARE(m_client->health, 20.0);
        QCOMPARE(m_client->motion.position.y, double(m_feet.y));
    }

    void eatingRestoresFoodOnlyWhenHungry()
    {
        join();
        QCOMPARE(m_client->food, 20);
        chat(u"/give raw_porkchop 2"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:raw_porkchop"))});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:raw_porkchop")), 2);

        chat(u"/feed 10"_s);
        QCOMPARE(m_client->food, 10);

        // Raw porkchop is worth 3 food points (content/qtcraft/items/raw_porkchop.json).
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:raw_porkchop")), 1);
        QCOMPARE(m_client->food, 13);

        // Eating does not heal directly any more.
        QCOMPARE(m_client->health, 20.0);
        chat(u"/feed 25"_s);
        QCOMPARE(m_client->food, 13);
    }

    void foodGatesRegenerationAndStarvationStopsAtAFloor()
    {
        join();
        fallDownShaft();
        QCOMPARE(m_client->health, 13.0);

        chat(u"/feed 17"_s);
        idle(100);
        QCOMPARE(m_client->health, 13.0);
        chat(u"/feed 18"_s);
        idle(int(QcHunger::regenerationInterval * QcPhysics::tickRate) + 1);
        QCOMPARE(m_client->health, 14.0);

        // Starving costs one point every 4 s down to one point: 13 points take 52 s.
        chat(u"/feed 0"_s);
        idle(int(13 * QcHunger::starvationInterval * QcPhysics::tickRate) + 10);
        QCOMPARE(m_client->health, 1.0);
        QVERIFY(!m_client->dead);
        idle(200);
        QCOMPARE(m_client->health, 1.0);

        // Respawning after a death fills the food again.
        chat(u"/kill"_s);
        send(QcMsg::Respawn{});
        QCOMPARE(m_client->food, 20);
    }

    void jumpingCostsFoodButStandingDoesNot()
    {
        join();
        idle(600);
        QCOMPARE(m_client->food, 20);
        // About 250 jumps at 0.02 exhaustion each: at least one and at most two food points.
        for (int i = 0; i < 3000; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            input.jump = true;
            m_client->connection.send(QcMsg::PlayerInput{input});
            pump();
            ticks(1);
        }
        QVERIFY2(m_client->food >= 18 && m_client->food <= 19, qPrintable(QString::number(m_client->food)));
    }

    void gameRuleNeedsAnOperatorAndAValue()
    {
        QcServerConfig config = notOperator();
        config.keepInventory = false;
        join(config);
        chat(u"/gamerule keepInventory"_s);
        QVERIFY(m_client->chatLines.last().contains(u"false"_s));
        chat(u"/gamerule keepInventory true"_s);
        QVERIFY(m_client->chatLines.last().contains(u"Only operators"_s));
        QVERIFY(!m_server->keepInventory());
        cleanup();
        join();
        chat(u"/gamerule keepInventory maybe"_s);
        QVERIFY(m_client->chatLines.last().startsWith(u"Usage"_s));
        QVERIFY(m_server->keepInventory());
    }

    void spawnItemAndSetBlockCommands()
    {
        join();
        chat(u"/spawnitem dirt 7"_s);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 7);

        chat(u"/spawnitem dirt 65"_s);
        chat(u"/spawnitem nothing"_s);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 7);

        chat(u"/setblock ~2 ~ ~1 oak_sapling"_s);
        QCOMPARE(m_server->world().blockAt(m_feet.offset(2, 0, 1)), std::optional<QcBlockId>(id("qtcraft:oak_sapling")));
        QCOMPARE(m_server->blockEntities().saplingsInOrder().size(), 1);

        chat(u"/setblock %1 %2 %3 glass"_s.arg(m_feet.x + 1).arg(m_feet.y).arg(m_feet.z + 2));
        QCOMPARE(m_server->world().blockAt(m_feet.offset(1, 0, 2)), std::optional<QcBlockId>(id("qtcraft:glass")));

        // Air breaks the block below the sapling without drops; with the next tick's neighbour updates
        // the sapling falls off and drops itself.
        chat(u"/setblock ~2 ~-1 ~1 air"_s);
        QCOMPARE(m_server->world().blockAt(m_feet.offset(2, 0, 1)), std::optional<QcBlockId>(id("qtcraft:oak_sapling")));
        m_server->tick();
        QCOMPARE(m_server->world().blockAt(m_feet.offset(2, 0, 1)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(droppedCount(item("qtcraft:oak_sapling")), 1);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 7);

        chat(u"/setblock ~ 9999 ~ glass"_s);
        QVERIFY(m_client->chatLines.last().startsWith(u"Usage"_s));
        chat(u"/setblock ~x ~ ~ glass"_s);
        QVERIFY(m_client->chatLines.last().startsWith(u"Usage"_s));

        cleanup();
        join(notOperator());
        chat(u"/spawnitem dirt"_s);
        chat(u"/setblock ~1 ~ ~ glass"_s);
        QVERIFY(m_server->items().items().isEmpty());
        QCOMPARE(m_server->world().blockAt(m_feet.offset(1, 0, 0)), std::optional<QcBlockId>(qcAirBlockId));
    }

    void appearancesAreCheckedAndShownToOthers()
    {
        join();

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient bob(std::move(clientEnd));

        QcAppearance look;
        look.outfit = u"qtcraft:miner"_s;
        look.hair = u"qtcraft:explorer"_s; // an outfit, not a hair style
        look.shirtColor = 0x112233;
        bob.hello(u"Bob"_s, look);
        pump();
        ticks(2);

        const quint32 bobId = std::ranges::find_if(m_client->spawns, [](const QcMsg::EntitySpawn &spawn) {
                                  return spawn.name == u"Bob"_s;
                              })->id;
        const QcMsg::EntitySpawn seen = m_client->spawns.value(bobId);
        QCOMPARE(seen.kind, QcEntityKind::Player);
        QCOMPARE(seen.appearance.outfit, u"qtcraft:miner"_s);
        QVERIFY(seen.appearance.hair.isEmpty());
        QCOMPARE(seen.appearance.shirtColor, 0x112233u);

        // Creature outfits are not for players.
        look.outfit = u"qtcraft:zombie"_s;
        bob.connection.send(QcMsg::SetAppearance{look});
        pump();
        ticks(2);
        QCOMPARE(m_client->spawns.value(bobId).appearance.outfit, QcAppearance().outfit);

        look.outfit = u"qtcraft:miner"_s;
        bob.connection.send(QcMsg::SetAppearance{look});
        pump();
        ticks(2);
        QCOMPARE(m_client->spawns.value(bobId).appearance.outfit, u"qtcraft:miner"_s);

        // A changed look reaches the others; unknown outfits fall back to the default.
        look.outfit = u"nomod:robe"_s;
        look.hair = u"qtcraft:long_hair"_s;
        bob.connection.send(QcMsg::SetAppearance{look});
        pump();
        ticks(2);
        QCOMPARE(m_client->spawns.value(bobId).appearance.outfit, QcAppearance().outfit);
        QCOMPARE(m_client->spawns.value(bobId).appearance.hair, u"qtcraft:long_hair"_s);

        // Capes (decision E-240): a known cape reaches the others; an unknown name or another kind
        // of appearance becomes no cape.
        look.cape = u"qtcraft:green_cape"_s;
        bob.connection.send(QcMsg::SetAppearance{look});
        pump();
        ticks(2);
        QCOMPARE(m_client->spawns.value(bobId).appearance.cape, u"qtcraft:green_cape"_s);
        for (const QString &wrong : {u"nomod:cape"_s, u"qtcraft:long_hair"_s, u"qtcraft:tunic"_s}) {
            look.cape = wrong;
            bob.connection.send(QcMsg::SetAppearance{look});
            pump();
            ticks(2);
            QVERIFY2(m_client->spawns.value(bobId).appearance.cape.isEmpty(), qPrintable(wrong));
        }

        // Using an item swings Bob's arm for everyone else.
        bob.connection.send(QcMsg::UseItem{m_feet.offset(1, -1, 0), QcBlockFace::Up});
        pump();
        ticks(2);
        QVERIFY((m_client->flags.value(bobId) & QcMsg::Swing) != 0);

        bob.connection.send(QcMsg::Disconnect{});
        pump();
        ticks(1);
    }

    void profileIsCheckedBeforeItIsTakenOver()
    {
        // Names: no control or format characters, trimmed, at most 32 characters, never empty.
        QCOMPARE(QcProfileValidation::playerName(u"  Ann\x07\n "_s), u"Ann"_s);
        QCOMPARE(QcProfileValidation::playerName(u"Bo\u200Bb"_s), u"Bob"_s);
        QCOMPARE(QcProfileValidation::playerName(QString(40, u'x')), QString(32, u'x'));
        QCOMPARE(QcProfileValidation::playerName(u" \t "_s), u"Player"_s);
        QCOMPARE(QcProfileValidation::playerName(u"Jörg Müller"_s), u"Jörg Müller"_s);

        // The limit counts code points and cuts between graphemes (decision E-232): 31 letters and
        // an emoji (two UTF-16 units) are 32 and stay whole; one letter more and the emoji goes
        // whole, no lone surrogate is left.
        const QString emoji = u"\U0001F600"_s;
        QCOMPARE(emoji.size(), 2);
        QCOMPARE(QcProfileValidation::playerName(QString(31, u'x') + emoji), QString(31, u'x') + emoji);
        QCOMPARE(QcProfileValidation::playerName(QString(32, u'x') + emoji), QString(32, u'x'));
        // A letter with a combining accent past the limit goes with its accent.
        QCOMPARE(QcProfileValidation::playerName(QString(31, u'x') + u"e\u0301"_s), QString(31, u'x'));
        // Unpaired surrogates (a high one alone, a low one alone, a high one at the end) are dropped.
        QCOMPARE(QcProfileValidation::playerName(u"An"_s + QChar(0xD83D) + u"n"_s + QChar(0xDE00) + u"e"_s + QChar(0xD83D)), u"Anne"_s);
        const QStringList names{QString(31, u'x') + emoji, QString(40, u'x') + emoji, u"a"_s + QChar(0xD800)};
        for (const QString &name : names) {
            const QString checked = QcProfileValidation::playerName(name);
            QVERIFY(checked.isValidUtf16());
            QVERIFY(QcNameText::codePoints(checked) <= QcProfileValidation::maxPlayerNameLength);
        }

        // Looks: only playable outfits and hair styles of the content, colours with RGB bits only.
        QcAppearance look;
        look.outfit = u"qtcraft:zombie"_s;
        look.hair = u"qtcraft:miner"_s;
        look.skinColor = 0xFF123456;
        const QcAppearance checked = QcProfileValidation::appearance(*m_content, look);
        QCOMPARE(checked.outfit, QcAppearance().outfit);
        QVERIFY(checked.hair.isEmpty());
        QCOMPARE(checked.skinColor, 0x123456u);
        look = {u"qtcraft:tunic"_s, u"qtcraft:cap"_s, 1, 2, 3, 4};
        QCOMPARE(QcProfileValidation::appearance(*m_content, look), look);
        // Capes: only capes of the content; a hair style or outfit given as cape becomes none.
        look.cape = u"qtcraft:green_cape"_s;
        QCOMPARE(QcProfileValidation::appearance(*m_content, look), look);
        look.cape = u"qtcraft:cap"_s;
        QVERIFY(QcProfileValidation::appearance(*m_content, look).cape.isEmpty());
        look.cape.clear();
        // Content without the default outfit: any playable outfit; without any: none.
        QcContentRegistry bare;
        QVERIFY(QcProfileValidation::appearance(bare, look).outfit.isEmpty());

        // Over the protocol: the name others see is the checked one, the armour slots are empty.
        join();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient bob(std::move(clientEnd));
        bob.hello(u"\x01 Bob\t"_s);
        pump();
        ticks(2);
        const auto seen = std::ranges::find_if(m_client->spawns, [](const QcMsg::EntitySpawn &spawn) { return spawn.name == u"Bob"_s; });
        QVERIFY(seen != m_client->spawns.end());
        QVERIFY(seen->equipment.isEmpty());

        bob.connection.send(QcMsg::Disconnect{});
        pump();
        ticks(1);
    }

    void aChangedLookBeforeLeavingLeavesNoGhost()
    {
        join();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient bob(std::move(clientEnd));

        bob.hello(u"Bob"_s);
        pump();
        ticks(2);
        const quint32 bobId = std::ranges::find_if(m_client->spawns, [](const QcMsg::EntitySpawn &spawn) {
                                  return spawn.name == u"Bob"_s;
                              })->id;
        QVERIFY(m_client->entities.contains(bobId));

        QcAppearance look;
        look.hair = u"qtcraft:long_hair"_s;
        bob.connection.send(QcMsg::SetAppearance{look});
        bob.connection.send(QcMsg::Disconnect{});
        pump();
        ticks(2);
        QVERIFY(!m_client->entities.contains(bobId));
    }

    void fallDamageFlashesForOthers()
    {
        join();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient bob(std::move(clientEnd));

        bob.hello(u"Bob"_s);
        pump();
        ticks(2);
        const quint32 bobId = std::ranges::find_if(m_client->spawns, [](const QcMsg::EntitySpawn &spawn) {
                                  return spawn.name == u"Bob"_s;
                              })->id;

        const QcVec3 p = bob.motion.position;
        const QcBlockPos feet{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        for (int dy = -1; dy >= -10; --dy) {
            m_server->world().setBlock(feet.offset(0, dy, 0), qcAirBlockId);
        }
        m_server->world().setBlock(feet.offset(0, -11, 0), id("qtcraft:stone"));

        for (quint32 sequence = 1; sequence <= 60; ++sequence) {
            QcPlayerInput input;
            input.sequence = sequence;
            bob.connection.send(QcMsg::PlayerInput{input});
            pump();
            ticks(1);
        }

        QVERIFY(bob.health < QcServer::playerMaxHealth);
        QVERIFY(m_client->hurt.contains(bobId));
    }
};

QTEST_GUILESS_MAIN(TestServerValidation)
#include "test_server_validation.moc"
