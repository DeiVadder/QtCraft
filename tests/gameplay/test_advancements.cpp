#include "common/protocol/QcChatText.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/advancements/QcAdvancementMatch.h"
#include "server/storage/QcPlayerRecords.h"
#include "server/storage/QcPlayerStorage.h"
#include "server/storage/QcSaveFile.h"

#include <QScopeGuard>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The adventure guide on the server (decision E-923): the triggers the gameplay reports, the
// location checks, the messages to the client (snapshot after joining, deltas with the toast),
// /advancement, the player file with advancements the content does not know, and the cost per tick.
// Ticks are stepped by hand.
class TestAdvancements : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    bool done(const char *name)
    {
        const QcAdvancementDefinition *definition = m_content->advancements().find(QString::fromLatin1(name));
        return definition && player().advancements.progress.isDone(*definition);
    }

    // A second player on the server, playing.
    std::unique_ptr<RawClient> joinOther()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        return other;
    }

    // The entries the client got after the snapshot, in order.
    QList<QcMsg::AdvancementProgress::Entry> deltas() const
    {
        QList<QcMsg::AdvancementProgress::Entry> entries;
        for (const QcMsg::AdvancementProgress &message : std::as_const(m_client->advancements)) {
            if (!message.reset) {
                entries.append(message.entries);
            }
        }
        return entries;
    }

    bool announced(const QString &name) const
    {
        const QList<QcMsg::AdvancementProgress::Entry> entries = deltas();
        return std::ranges::any_of(entries, [&name](const auto &entry) { return entry.advancement == name && entry.announce && entry.done; });
    }

    void hold(const char *name)
    {
        chat(u"/give %1"_s.arg(QString::fromLatin1(name)));
        send(QcMsg::SelectHotbarSlot{slotOf(item(name))});
    }

    void trigger(const QcAdvancementEvent &event) { m_server->systems().advancements.trigger(player(), event); }

    quint32 spawnMob(const char *kind, int dx, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, static_cast<double>(m_feet.y), m_feet.z + dz + 0.5};
        return m_server->mobs().spawn(*m_content->mobIndex(QString::fromLatin1(kind)), at, m_server->entities().newId(), false, true);
    }

    // The base content with two advancements for uses on creatures: a bone and wheat.
    static std::shared_ptr<const QcContentRegistry> contentWithUses()
    {
        auto content = std::make_shared<QcContentRegistry>(*QcTestUtil::loadContent());
        for (const char *item : {"bone", "wheat"}) {
            QcAdvancementDefinition use;
            use.name = u"test:%1_on_creature"_s.arg(QString::fromLatin1(item));
            use.icon = u"qtcraft:stick"_s;
            QcAdvancementCriterion criterion{u"use"_s, QcAdvancementTrigger::ItemUsedOnEntity, {}};
            criterion.conditions.item = u"qtcraft:"_s + QString::fromLatin1(item);
            use.criteria = {criterion};
            use.requirements = {{u"use"_s}};
            content->advancements().add(use);
        }
        return content;
    }

private slots:
    void init() { join(); }

    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void joiningSendsTheWholeProgressOnce()
    {
        QVERIFY(!m_client->advancements.isEmpty());
        QVERIFY(m_client->advancements.constFirst().reset);
        QVERIFY(m_client->advancements.constFirst().entries.isEmpty());
        ticks(5);
        QCOMPARE(std::ranges::count_if(m_client->advancements, [](const auto &message) { return message.reset; }), 1);
    }

    void obtainingAnItemReachesTheRootWithAToast()
    {
        QVERIFY(!done("qtcraft:adventure"));
        chat(u"/give oak_log"_s);
        ticks(1);
        QVERIFY(done("qtcraft:adventure"));
        QVERIFY(announced(u"qtcraft:adventure"_s));
        // Another log changes nothing more.
        const qsizetype before = deltas().size();
        chat(u"/give birch_log"_s);
        ticks(1);
        QCOMPARE(deltas().size(), before);
    }

    void craftingCountsTheResult()
    {
        chat(u"/give oak_planks 4"_s);
        send(QcMsg::CraftRecipe{u"qtcraft:crafting_table"_s, 1});
        ticks(1);
        QVERIFY(done("qtcraft:crafting_table"));
        QVERIFY(!done("qtcraft:wooden_pickaxe"));
    }

    void placingAndBreakingBlocksCount()
    {
        hold("qtcraft:torch");
        send(QcMsg::UseItem{m_feet.offset(2, -1, 0), QcBlockFace::Up});
        QVERIFY(done("qtcraft:torch"));

        chat(u"/gamemode creative"_s);
        const QcBlockPos stone = m_feet.offset(-2, 0, 0);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        send(QcMsg::StartMining{stone});
        send(QcMsg::FinishMining{stone});
        QCOMPARE(m_server->world().blockAt(stone), std::optional<QcBlockId>(qcAirBlockId));
        QVERIFY(done("qtcraft:stone_age"));
    }

    void killingAMonsterCounts()
    {
        hold("qtcraft:diamond_sword");
        const quint32 zombie = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:zombie"_s), {m_feet.x + 1.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 500);
        for (int blow = 0; blow < 10 && m_server->mobs().find(zombie); ++blow) {
            send(QcMsg::AttackEntity{zombie});
            ticks(20);
        }
        QVERIFY(!m_server->mobs().find(zombie));
        QVERIFY(done("qtcraft:monster_hunter"));
    }

    void ridingAMinecartCounts()
    {
        m_server->world().setBlock(m_feet.offset(2, 0, 0), id("qtcraft:rail"));
        ticks(2);
        hold("qtcraft:minecart");
        send(QcMsg::UseItem{m_feet.offset(2, 0, 0), QcBlockFace::Up});
        QCOMPARE(m_server->carts().all().size(), 1);
        send(QcMsg::UseEntity{m_server->carts().all().constFirst().id});
        QVERIFY(done("qtcraft:minecart"));
        QVERIFY(!done("qtcraft:boat"));
    }

    void levelsAndEffectsCount()
    {
        chat(u"/xp add Raw 29 levels"_s);
        ticks(1);
        QVERIFY(!done("qtcraft:level_thirty"));
        chat(u"/xp add Raw 1 levels"_s);
        ticks(1);
        QVERIFY(done("qtcraft:level_thirty"));

        // The base tree has no effect advancement: the event reaches matching like the others.
        const QcAdvancementConditions poison{.effect = u"qtcraft:poison"_s};
        QVERIFY(QcAdvancementMatch::matches(*m_content, poison, {.trigger = QcAdvancementTrigger::EffectGained, .effect = u"qtcraft:poison"_s}));
        QVERIFY(!QcAdvancementMatch::matches(*m_content, poison, {.trigger = QcAdvancementTrigger::EffectGained, .effect = u"qtcraft:hunger"_s}));
    }

    void locationIsCheckedOnlyAfterMoving()
    {
        ticks(QcServerAdvancements::locationInterval * 2);
        const quint64 checks = m_server->systems().advancements.locationChecks();
        ticks(QcServerAdvancements::locationInterval * 5);
        // Standing still costs no location checks.
        QCOMPARE(m_server->systems().advancements.locationChecks(), checks);
        QVERIFY(!done("qtcraft:deep_down"));

        chat(u"/tp Raw %1 10 %2"_s.arg(m_feet.x).arg(m_feet.z));
        ticks(QcServerAdvancements::locationInterval);
        QVERIFY(done("qtcraft:deep_down"));
        QVERIFY(!done("qtcraft:mountain_top"));
        QVERIFY(!done("qtcraft:village"));
        QVERIFY(m_server->systems().advancements.locationChecks() <= checks + 2);
    }

    void eventsMatchTheirConditions()
    {
        trigger({.trigger = QcAdvancementTrigger::ItemSmelted, .item = u"qtcraft:gold_ingot"_s});
        QVERIFY(!done("qtcraft:iron"));
        trigger({.trigger = QcAdvancementTrigger::ItemSmelted, .item = u"qtcraft:iron_ingot"_s});
        QVERIFY(done("qtcraft:iron"));
        trigger({.trigger = QcAdvancementTrigger::ItemUsedOnBlock, .item = u"qtcraft:stick"_s, .block = u"qtcraft:dirt"_s});
        QVERIFY(!done("qtcraft:farming"));
        trigger({.trigger = QcAdvancementTrigger::ItemUsedOnBlock, .item = u"qtcraft:iron_hoe"_s, .block = u"qtcraft:dirt"_s});
        QVERIFY(done("qtcraft:farming"));
        trigger({.trigger = QcAdvancementTrigger::EntityBred, .entity = u"qtcraft:cow"_s});
        trigger({.trigger = QcAdvancementTrigger::EntityTamed, .entity = u"qtcraft:wolf"_s});
        trigger({.trigger = QcAdvancementTrigger::VillagerTrade, .item = u"qtcraft:emerald"_s});
        trigger({.trigger = QcAdvancementTrigger::ItemEnchanted, .item = u"qtcraft:book"_s});
        QVERIFY(done("qtcraft:breeding") && done("qtcraft:tame") && done("qtcraft:trade") && done("qtcraft:enchanter"));
        // A tag of items matches the blocks it holds (beds).
        trigger({.trigger = QcAdvancementTrigger::BlockPlaced, .block = u"qtcraft:blue_bed"_s});
        QVERIFY(done("qtcraft:shelter"));
    }

    void smeltingCountsWhenTheOutputIsTaken()
    {
        const QcBlockPos furnace = openFurnace();
        chat(u"/give raw_iron 1"_s);
        chat(u"/give coal 1"_s);
        move(slotOf(item("qtcraft:raw_iron")), inputSlot);
        move(slotOf(item("qtcraft:coal")), fuelSlot);
        for (int i = 0; i < 20 * 12 && m_server->blockEntities().machine(furnace)->stacks[2].isEmpty(); ++i) {
            ticks(1);
        }
        QCOMPARE(m_server->blockEntities().machine(furnace)->stacks[2].count, 1);
        // Smelted but not taken: nothing yet.
        QVERIFY(!done("qtcraft:iron"));
        move(outputSlot, 20);
        QVERIFY(done("qtcraft:iron"));
    }

    void breedingTamingAndFeedingCount()
    {
        cleanup();
        m_content = contentWithUses();
        join();

        const quint32 first = spawnMob("qtcraft:cow", 2, 0);
        const quint32 second = spawnMob("qtcraft:cow", -2, 1);
        hold("qtcraft:wheat");
        chat(u"/give wheat 1"_s);
        send(QcMsg::UseEntity{first});
        send(QcMsg::UseEntity{second});
        QVERIFY(done("test:wheat_on_creature"));
        for (int i = 0; i < 100 && !done("qtcraft:breeding"); ++i) {
            ticks(1);
        }
        QVERIFY(done("qtcraft:breeding"));

        // Taming feeds bones; it counts as taming, not as a use on the creature.
        hold("qtcraft:bone");
        chat(u"/give bone 30"_s);
        const quint32 wolf = spawnMob("qtcraft:wolf", 0, 2);
        for (int i = 0; i < 30 && !m_server->mobs().find(wolf)->isTame(); ++i) {
            send(QcMsg::UseEntity{wolf});
        }
        QVERIFY(m_server->mobs().find(wolf)->isTame());
        QVERIFY(done("qtcraft:tame"));
        QVERIFY(!done("test:bone_on_creature"));
    }

    void damagedAdvancementsCostOnlyTheProgress()
    {
        QTemporaryDir dir;
        const QcPlayerStorage storage(dir.path(), m_content);
        QcPlayerData data;
        data.motion.position = {0.5, 70.0, 0.5};
        data.experience = QcExperience(5, 0);
        QVERIFY(data.advancements.grant(u"qtcraft:iron"_s, u"iron"_s));
        const QcResult<QcSaveFile::Payload> payload = QcSaveFile::parse(storage.bytes(data), u"p"_s, "QCPL", 9);
        QVERIFY(payload);

        // The advancements are the last field: a byte array whose criteria count is forged.
        QByteArray damaged = payload->bytes.chopped(static_cast<qsizetype>(sizeof(quint32)) + data.advancements.bytes().size());
        QDataStream out(&damaged, QIODevice::Append);
        out.setVersion(QcSaveFile::streamVersion);
        out << QByteArray::fromHex("0001" "00000001" "00000002" "0078" "fffffff0");
        const QcPlayerStorage::Result read = storage.parse(u"Raw"_s, QcSaveFile::bytes("QCPL", damaged, 9));
        QVERIFY(read && read->player);
        QCOMPARE(read->player->experience.level(), 5);
        QVERIFY(read->player->advancements.isEmpty());
        QVERIFY(std::ranges::any_of(read->warnings, [](const QString &warning) { return warning.contains(u"advancements are damaged"_s); }));
    }

    void requirementsNeedEveryGroup()
    {
        chat(u"/give diamond_helmet"_s);
        chat(u"/give diamond_boots"_s);
        ticks(1);
        QVERIFY(!done("qtcraft:diamond_armour"));
        QCOMPARE(player().advancements.progress.criteria(u"qtcraft:diamond_armour"_s).size(), 2);
        chat(u"/give diamond_chestplate"_s);
        chat(u"/give diamond_leggings"_s);
        ticks(1);
        QVERIFY(done("qtcraft:diamond_armour"));
        QVERIFY(announced(u"qtcraft:diamond_armour"_s));
    }

    void commandsGrantRevokeAndQuery()
    {
        chat(u"/advancement grant Raw iron"_s);
        QVERIFY(done("qtcraft:iron"));
        chat(u"/advancement grant Raw everything"_s);
        for (const QcAdvancementDefinition &advancement : m_content->advancements().items()) {
            QVERIFY2(player().advancements.progress.isDone(advancement), qPrintable(advancement.name));
        }
        chat(u"/advancement query Raw"_s);
        QVERIFY(m_client->chatKeys.constLast().key.contains(u"has reached"_s));
        chat(u"/advancement revoke Raw qtcraft:iron"_s);
        QVERIFY(!done("qtcraft:iron"));
        chat(u"/advancement revoke Raw everything"_s);
        QVERIFY(player().advancements.progress.isEmpty());
        chat(u"/advancement grant Raw nothing_like_this"_s);
        QVERIFY(m_client->chatKeys.constLast().key.contains(u"Unknown advancement"_s));

        // The client heard of each change: the grant announced, the revoke not.
        ticks(1);
        QVERIFY(std::ranges::any_of(deltas(), [](const auto &entry) { return entry.advancement == u"qtcraft:iron"_s && !entry.done; }));
    }

    // The other players read it in the chat (decision E-924), with the title as a content key and the
    // wording by the frame; the player himself has the toast. Neither an advancement with
    // "announce": false nor the game rule announceAdvancements off says anything.
    void reachingOneIsAnnouncedToTheOthers()
    {
        cleanup();
        auto content = std::make_shared<QcContentRegistry>(*QcTestUtil::loadContent());
        QcAdvancementDefinition quiet = *content->advancements().find(u"qtcraft:map"_s);
        quiet.announce = false;
        content->advancements().add(quiet);
        m_content = content;
        const auto restore = qScopeGuard([this] { m_content = QcTestUtil::loadContent(); });
        join();
        const std::unique_ptr<RawClient> second = joinOther();
        RawClient &other = *second;
        const auto announcements = [&other] {
            return std::ranges::count_if(other.chatKeys, [](const QcMsg::ChatMessage &line) { return line.key.endsWith(u"[%2]"_s); });
        };

        chat(u"/give oak_log"_s);
        ticks(1);
        QCOMPARE(announcements(), 1);
        const QcMsg::ChatMessage &line = other.chatKeys.constLast();
        QCOMPARE(line.key, u"%1 has made the advancement [%2]"_s);
        QCOMPARE(line.arguments, (QStringList{u"Raw"_s, u"Adventure Guide"_s}));
        QCOMPARE(line.contentKeys, (QStringList{QString(), u"advancement.qtcraft.adventure"_s}));
        QVERIFY(std::ranges::none_of(m_client->chatKeys, [](const QcMsg::ChatMessage &own) { return own.key.contains(u"has made"_s); }));

        chat(u"/advancement grant Raw diamonds"_s);
        QCOMPARE(other.chatKeys.constLast().key, u"%1 has reached the goal [%2]"_s);
        chat(u"/advancement grant Raw diamond_armour"_s);
        QCOMPARE(other.chatKeys.constLast().key, u"%1 has completed the challenge [%2]"_s);
        QCOMPARE(announcements(), 3);

        chat(u"/advancement grant Raw map"_s);
        QCOMPARE(announcements(), 3);
        chat(u"/gamerule announceAdvancements false"_s);
        chat(u"/advancement grant Raw iron"_s);
        QVERIFY(done("qtcraft:iron"));
        QCOMPARE(announcements(), 3);
    }

    // /advancement grant … everything: at most announcementsPerTick lines, the rest folded into one
    // with its count and the plural; after a revoke, reaching one again is announced again.
    void aFloodOfReachedOnesFoldsIntoOneLine()
    {
        const std::unique_ptr<RawClient> other = joinOther();
        const auto lines = [&other] {
            return std::ranges::count_if(other->chatKeys, [](const QcMsg::ChatMessage &line) { return line.key.endsWith(u"[%2]"_s) || line.key.contains(u"%n more"_s); });
        };
        const qsizetype announced = std::ranges::count_if(m_content->advancements().items(), [](const auto &advancement) { return advancement.announce; });
        QVERIFY(announced > QcServerAdvancements::announcementsPerTick + 1);

        chat(u"/advancement grant Raw everything"_s);
        ticks(1);
        QCOMPARE(lines(), QcServerAdvancements::announcementsPerTick + 1);
        const QcMsg::ChatMessage &folded = other->chatKeys.constLast();
        const qsizetype rest = announced - QcServerAdvancements::announcementsPerTick;
        QCOMPARE(folded.key, u"%1 has made %n more advancements"_s);
        QCOMPARE(folded.arguments, (QStringList{u"Raw"_s, QString::number(rest)}));
        QCOMPARE(folded.text, u"Raw has made %1 more advancements"_s.arg(rest));
        QCOMPARE(QcChatText::display(folded), folded.text);
        ticks(2);
        QCOMPARE(lines(), QcServerAdvancements::announcementsPerTick + 1);

        chat(u"/advancement revoke Raw everything"_s);
        ticks(1);
        chat(u"/advancement grant Raw iron"_s);
        ticks(1);
        QCOMPARE(lines(), QcServerAdvancements::announcementsPerTick + 2);
        QVERIFY(other->chatKeys.constLast().key.endsWith(u"[%2]"_s));
    }

    void commandsNeedAnOperator()
    {
        cleanup();
        join(notOperator());
        chat(u"/advancement grant Raw everything"_s);
        QVERIFY(player().advancements.progress.isEmpty());
    }

    void playerFileKeepsUnknownAdvancements()
    {
        QTemporaryDir dir;
        const QcPlayerStorage storage(dir.path(), m_content);
        QcPlayerData data;
        data.motion.position = {0.5, 70.0, 0.5};
        QVERIFY(data.advancements.grant(u"qtcraft:iron"_s, u"iron"_s));
        QVERIFY(data.advancements.grant(u"removedmod:quest"_s, u"step"_s));
        QVERIFY(storage.write(u"Raw"_s, data));
        const QcPlayerStorage::Result read = storage.read(u"Raw"_s);
        QVERIFY(read && read->player);
        QCOMPARE(read->player->advancements, data.advancements);
        QVERIFY(read->player->advancements.has(u"removedmod:quest"_s, u"step"_s));
    }

    void progressComesBackAfterRejoining()
    {
        chat(u"/advancement grant Raw iron"_s);
        player().advancements.progress.grant(u"removedmod:quest"_s, u"step"_s);
        const QcPlayerData saved = QcPlayerRecords::of(player(), *m_content).player;
        QcServerClient fresh;
        QcPlayerRecords::restore(fresh, saved);
        QCOMPARE(fresh.advancements.progress, player().advancements.progress);
    }

    void costPerTickStaysFlat()
    {
        // Nothing happening: no criterion is compared, whatever the number of ticks.
        ticks(QcServerAdvancements::locationInterval * 3);
        const quint64 comparisons = m_server->systems().advancements.comparisons();
        ticks(QcServerAdvancements::locationInterval * 10);
        QCOMPARE(m_server->systems().advancements.comparisons(), comparisons);

        // An event compares only the criteria of its trigger that are not met yet.
        trigger({.trigger = QcAdvancementTrigger::RideVehicle, .vehicle = u"boat"_s});
        QCOMPARE(m_server->systems().advancements.comparisons(), comparisons + 2);
        trigger({.trigger = QcAdvancementTrigger::RideVehicle, .vehicle = u"boat"_s});
        QCOMPARE(m_server->systems().advancements.comparisons(), comparisons + 3);
    }
};

QTEST_GUILESS_MAIN(TestAdvancements)
#include "test_advancements.moc"
