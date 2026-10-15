#include "QcArmour.h"
#include "QcProtocolCodec.h"
#include "QcSaveFile.h"
#include "QcServerCombat.h"
#include "QcServerPersistence.h"
#include "QcServerStands.h"
#include "QcServerTestFixture.h"
#include "QcShowcaseArmourArea.h"
#include "QcShowcasePlanner.h"
#include "QcEntityCodec.h"
#include "QcRegionFile.h"
#include "QcTestUtil.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// Armour stands on the server (decisions E-238, E-239): placing one, dressing it and undressing it
// by right clicks, breaking it with its drops, the commands, saving, what clients see of it and the
// stands of the showcase world.
class TestArmourStands : public QcServerTestFixture
{
    Q_OBJECT

    // Places a stand from the item two blocks north of the player (the player looks north).
    quint32 placeStand()
    {
        chat(u"/give armor_stand"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:armor_stand"))});
        const QcBlockPos ground = m_feet.offset(0, -1, -2);
        send(QcMsg::UseItem{ground, QcBlockFace::Up, {0.5, 1.0, 0.5}});
        return m_server->stands().stands().isEmpty() ? 0 : m_server->stands().stands().constLast().id;
    }

    const QcArmourStand *stand(quint32 id) { return m_server->stands().find(id); }

    // Ticks until the attack cooldown is over, and ticks of the break window.
    static constexpr int cooldownTicks = int(QcServerCombat::playerAttackCooldown * QcPhysics::tickRate) + 1;
    static constexpr int breakWindowTicks = int(QcArmourStand::breakWindow * QcPhysics::tickRate);
    static_assert(cooldownTicks + 1 < breakWindowTicks, "a second hit after the cooldown must fit into the break window");

    // A second raw client on the server.
    std::unique_ptr<RawClient> joinOther()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        pump();
        ticks(3);
        return other;
    }

    // Fills the world up to the stand limit with stands high above the stage.
    void fillWithStands()
    {
        QcArmourStand far;
        far.motion.position = {m_feet.x + 0.5, 300.0, m_feet.z + 0.5};
        far.item = u"qtcraft:armor_stand"_s;
        for (quint32 i = 0; m_server->stands().stands().size() < QcArmourStandManager::maxStands; ++i) {
            far.id = 4'000'000'000u + i;
            QVERIFY(m_server->stands().add(far));
        }
    }

    // Turns the player's view (yaw, pitch) and lets the server take the input.
    void look(float yaw, float pitch)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = yaw;
        input.pitch = pitch;
        m_client->connection.send(QcMsg::PlayerInput{input});
        pump();
        ticks(1);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void standsArePlacedFacingThePlayerAndRefusedWithoutRoom()
    {
        join();
        const quint32 id = placeStand();
        QVERIFY(id != 0);
        QCOMPARE(m_server->stands().stands().size(), 1);
        QCOMPARE(stand(id)->motion.position, (QcVec3{m_feet.x + 0.5, double(m_feet.y), m_feet.z - 1.5}));
        // The player looks north (yaw 0): the stand looks south at them.
        QCOMPARE(stand(id)->yaw, 180.0f);
        QCOMPARE(stand(id)->item, u"qtcraft:armor_stand"_s);
        QCOMPARE(countOf(item("qtcraft:armor_stand")), 0);

        // The same place again, and a place under a block: no room, the item stays.
        placeStand();
        QCOMPARE(m_server->stands().stands().size(), 1);
        m_server->world().setBlock(m_feet.offset(2, 1, -2), QcServerTestFixture::id("qtcraft:stone"));
        send(QcMsg::UseItem{m_feet.offset(2, -1, -2), QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QCOMPARE(m_server->stands().stands().size(), 1);
        QCOMPARE(countOf(item("qtcraft:armor_stand")), 1);

        QCOMPARE(QcServerStands::facingYaw(90.0f), -90.0f);
        QCOMPARE(QcServerStands::facingYaw(10.0f), 180.0f);
        QCOMPARE(QcServerStands::facingYaw(200.0f), 0.0f);
    }

    void rightClicksDressAndUndressTheStand()
    {
        join();
        const quint32 id = placeStand();
        chat(u"/give iron_chestplate"_s);
        chat(u"/give iron_sword"_s);

        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:iron_chestplate"))});
        send(QcMsg::UseEntity{id});
        QCOMPARE(stand(id)->equipment.at(QcEquipmentSlot::Chest).item, item("qtcraft:iron_chestplate"));
        QCOMPARE(countOf(item("qtcraft:iron_chestplate")), 0);

        // Anything else goes into the stand's hand.
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:iron_sword"))});
        send(QcMsg::UseEntity{id});
        QCOMPARE(stand(id)->held.item, item("qtcraft:iron_sword"));

        // An empty hand aimed at the chest takes the chestplate, then (nothing left there) the sword.
        send(QcMsg::SelectHotbarSlot{8});
        look(0.0f, -20.0f);
        send(QcMsg::UseEntity{id});
        QVERIFY(stand(id)->equipment.isEmpty());
        QCOMPARE(m_client->stacks[8].item, item("qtcraft:iron_chestplate"));
        send(QcMsg::SelectHotbarSlot{7});
        send(QcMsg::UseEntity{id});
        QVERIFY(stand(id)->held.isEmpty());
        QCOMPARE(m_client->stacks[7].item, item("qtcraft:iron_sword"));
    }

    void bodyPartsFollowTheHeight()
    {
        QCOMPARE(QcServerStands::slotAtHeight(0.1), QcEquipmentSlot::Feet);
        QCOMPARE(QcServerStands::slotAtHeight(0.5), QcEquipmentSlot::Legs);
        QCOMPARE(QcServerStands::slotAtHeight(1.0), QcEquipmentSlot::Chest);
        QCOMPARE(QcServerStands::slotAtHeight(1.7), QcEquipmentSlot::Head);
    }

    void twoQuickHitsBreakTheStandWithItsDrops()
    {
        join();
        const quint32 id = placeStand();
        // Stands are no creatures.
        QVERIFY(!m_server->mobs().find(id));
        chat(u"/armorstand nearest equip golden"_s);
        QCOMPARE(QcArmour::points(*m_content, stand(id)->equipment), 11);

        // One hit only wobbles it; after the break window the next hit is a first hit again.
        send(QcMsg::AttackEntity{id});
        QVERIFY(stand(id));
        idle(breakWindowTicks + 1);
        send(QcMsg::AttackEntity{id});
        QVERIFY(stand(id));
        // A second hit during the attack cooldown does not count, one after it does.
        send(QcMsg::AttackEntity{id});
        QVERIFY(stand(id));
        idle(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        QVERIFY(!stand(id));
        QCOMPARE(droppedCount(item("qtcraft:armor_stand")), 1);
        QCOMPARE(droppedCount(item("qtcraft:golden_chestplate")), 1);
        QCOMPARE(droppedCount(item("qtcraft:golden_boots")), 1);
    }

    void twoHitsInOneTickDoNotBreakTheStand()
    {
        join();
        const quint32 id = placeStand();
        // Both hits arrive before the server ticks: the second one falls into the cooldown.
        m_client->connection.send(QcMsg::AttackEntity{id});
        m_client->connection.send(QcMsg::AttackEntity{id});
        pump();
        ticks(1);
        QVERIFY(stand(id));
        QCOMPARE(stand(id)->hitBy, m_client->entityId);
    }

    void onlyTheSamePlayerBreaksTheStand()
    {
        join();
        const quint32 id = placeStand();
        const std::unique_ptr<RawClient> other = joinOther();
        const auto otherHits = [&] {
            other->connection.send(QcMsg::AttackEntity{id});
            pump();
            ticks(1);
        };

        // Two players hitting once each within the window: it only wobbles, and the last hitter
        // starts a new window.
        send(QcMsg::AttackEntity{id});
        otherHits();
        QVERIFY(stand(id));
        QCOMPARE(stand(id)->hitBy, other->entityId);
        idle(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        QVERIFY(stand(id));
        QCOMPARE(stand(id)->hitBy, m_client->entityId);

        // The same player's second hit after the cooldown breaks it.
        idle(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        QVERIFY(!stand(id));
        QCOMPARE(droppedCount(item("qtcraft:armor_stand")), 1);
    }

    void standsBehindWallsOrOutOfReachAreNotTouched()
    {
        join();
        const quint32 standId = placeStand();
        chat(u"/give iron_helmet"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:iron_helmet"))});

        // A wall between the player and the stand (one block north, two high, three wide).
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = 0; dy <= 2; ++dy) {
                m_server->world().setBlock(m_feet.offset(dx, dy, -1), id("qtcraft:stone"));
            }
        }
        send(QcMsg::UseEntity{standId});
        QVERIFY(stand(standId)->equipment.isEmpty());
        QCOMPARE(countOf(item("qtcraft:iron_helmet")), 1);
        for (int i = 0; i < 3; ++i) {
            send(QcMsg::AttackEntity{standId});
            idle(cooldownTicks);
        }
        QVERIFY(stand(standId));
        QCOMPARE(stand(standId)->hitBy, quint32(0));

        // Without the wall but far away: the same.
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = 0; dy <= 2; ++dy) {
                m_server->world().setBlock(m_feet.offset(dx, dy, -1), qcAirBlockId);
            }
        }
        m_server->stands().find(standId)->motion.position.z -= 10.0;
        send(QcMsg::UseEntity{standId});
        QVERIFY(stand(standId)->equipment.isEmpty());
        send(QcMsg::AttackEntity{standId});
        QCOMPARE(stand(standId)->hitBy, quint32(0));

        // Back in reach and sight, the helmet goes on.
        m_server->stands().find(standId)->motion.position.z += 10.0;
        send(QcMsg::UseEntity{standId});
        QCOMPARE(stand(standId)->equipment.at(QcEquipmentSlot::Head).item, item("qtcraft:iron_helmet"));
    }

    void theStandLimitRefusesPlacingAndSummoning()
    {
        join();
        fillWithStands();
        QCOMPARE(placeStand(), quint32(4'000'000'000u + QcArmourStandManager::maxStands - 1));
        QCOMPARE(m_server->stands().stands().size(), qsizetype(QcArmourStandManager::maxStands));
        QCOMPARE(countOf(item("qtcraft:armor_stand")), 1);
        QCOMPARE(m_client->chatKeys.constLast().key, u"Too many armour stands in the world (at most %1)"_s);
        QCOMPARE(m_client->chatKeys.constLast().arguments, QStringList{QString::number(QcArmourStandManager::maxStands)});

        chat(u"/give stick"_s);
        chat(u"/summon armor_stand"_s);
        QCOMPARE(m_server->stands().stands().size(), qsizetype(QcArmourStandManager::maxStands));
        QCOMPARE(m_client->chatKeys.constLast().key, u"Too many armour stands in the world (at most %1)"_s);
    }

    void replacedPiecesOfAStandFallToTheGround()
    {
        join();
        const quint32 id = placeStand();
        chat(u"/armorstand nearest equip golden"_s);
        chat(u"/armorstand nearest equip iron_sword"_s);
        QCOMPARE(droppedCount(item("qtcraft:golden_chestplate")), 0);

        // Iron replaces every golden piece; the sword is not touched.
        chat(u"/armorstand nearest equip iron"_s);
        for (const char *piece : {"qtcraft:golden_helmet", "qtcraft:golden_chestplate", "qtcraft:golden_leggings", "qtcraft:golden_boots"}) {
            QCOMPARE(droppedCount(item(piece)), 1);
        }
        QCOMPARE(droppedCount(item("qtcraft:iron_sword")), 0);

        // Clear drops the rest.
        chat(u"/armorstand nearest clear"_s);
        QVERIFY(stand(id)->equipment.isEmpty() && stand(id)->held.isEmpty());
        QCOMPARE(droppedCount(item("qtcraft:iron_chestplate")), 1);
        QCOMPARE(droppedCount(item("qtcraft:iron_sword")), 1);
    }

    void commandsPoseTheStandAndCheckTheirValues()
    {
        join();
        const quint32 id = placeStand();
        chat(u"/armorstand nearest pose right_arm -90 0 10"_s);
        QCOMPARE(stand(id)->pose.at(QcStandPose::Part::RightArm), (QcStandPose::Rotation{-90.0f, 0.0f, 10.0f}));
        chat(u"/armorstand nearest small true"_s);
        chat(u"/armorstand %1 name Sir Bob"_s.arg(id));
        // A small stand is low: the player looks down at it.
        look(0.0f, -35.0f);
        chat(u"/armorstand looking showname true"_s);
        QVERIFY(stand(id)->pose.small);
        QCOMPARE(stand(id)->name, u"Sir Bob"_s);
        QVERIFY(stand(id)->pose.showName);

        // Wrong values change nothing and get the usage.
        const QcArmourStand before = *stand(id);
        chat(u"/armorstand nearest pose head nan 0 0"_s);
        chat(u"/armorstand nearest rotation"_s);
        chat(u"/armorstand nearest equip nothing_at_all"_s);
        QCOMPARE(*stand(id), before);
        QVERIFY(m_client->chatKeys.constLast().key.startsWith(u"Usage: /armorstand"_s));
        // Every switch names its values.
        for (const QString &flag : {u"arms"_s, u"plate"_s, u"small"_s, u"showname"_s}) {
            QVERIFY(m_client->chatKeys.constLast().key.contains(flag + u" <true|false>"_s));
        }
        chat(u"/armorstand 999999 arms false"_s);
        QCOMPARE(m_client->chatKeys.constLast().key, u"No armour stand found: %1"_s);

        chat(u"/armorstand nearest reset"_s);
        QCOMPARE(stand(id)->pose, QcStandPose());
    }

    void summonTakesProperties()
    {
        join();
        const qsizetype creatures = m_server->mobs().mobs().size();
        chat(u"/summon armor_stand equip=iron pose=head,20,0,0 small arms=false name=Tin"_s);
        QCOMPARE(m_server->stands().stands().size(), 1);
        QCOMPARE(m_server->mobs().mobs().size(), creatures);
        const QcArmourStand &summoned = m_server->stands().stands().constFirst();
        QCOMPARE(QcArmour::points(*m_content, summoned.equipment), 15);
        QCOMPARE(summoned.pose.at(QcStandPose::Part::Head).x, 20.0f);
        QVERIFY(summoned.pose.small && !summoned.pose.arms);
        QCOMPARE(summoned.name, u"Tin"_s);

        chat(u"/summon armor_stand fly=true"_s);
        QCOMPARE(m_server->stands().stands().size(), 1);
        QCOMPARE(m_client->chatKeys.constLast().key, u"Invalid armour stand option: %1"_s);
        // Creatures are still summoned as before.
        chat(u"/summon pig"_s);
        QCOMPARE(m_server->mobs().mobs().size(), creatures + 1);
    }

    void clientsSeeStandsAndTheirChanges()
    {
        join();
        const quint32 id = placeStand();
        ticks(2);
        QVERIFY(m_client->spawns.contains(id));
        QCOMPARE(m_client->spawns.value(id).kind, QcEntityKind::ArmourStand);
        QVERIFY(m_client->spawns.value(id).equipment.isEmpty());

        chat(u"/armorstand nearest equip diamond"_s);
        chat(u"/armorstand nearest pose left_leg 30 0 0"_s);
        chat(u"/armorstand nearest cape green_cape"_s);
        ticks(2);
        const QcMsg::EntitySpawn spawn = m_client->spawns.value(id);
        QCOMPARE(QcArmour::points(*m_content, spawn.equipment), 20);
        // The cape travels in the look's cape field (E-239).
        QCOMPARE(spawn.appearance.cape, u"qtcraft:green_cape"_s);
        QCOMPARE(spawn.stand.at(QcStandPose::Part::LeftLeg).x, 30.0f);
        QCOMPARE(spawn.yaw, 180.0f);

        // Stands never move: no moves are sent for them.
        QVERIFY(!m_client->flags.contains(id));

        send(QcMsg::AttackEntity{id});
        idle(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        ticks(2);
        QVERIFY(!m_client->entities.contains(id));
    }

    void standsAreSavedWithTheWorld()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        placeStand();
        chat(u"/armorstand nearest equip leather"_s);
        chat(u"/armorstand nearest equip iron_sword"_s);
        chat(u"/armorstand nearest pose head 0 30 0"_s);
        chat(u"/armorstand nearest name Keeper"_s);
        chat(u"/armorstand nearest cape green_cape"_s);
        const QcArmourStand saved = m_server->stands().stands().constFirst();
        QCOMPARE(saved.cape, u"qtcraft:green_cape"_s);
        QVERIFY(m_server->save().has_value());
        // Saved with the entities of its chunk (decision E-366).
        QVERIFY(!QDir(dir.filePath(u"entities"_s)).entryList({u"r.*.qen"_s}, QDir::Files).isEmpty());
        m_client.reset();
        m_server.reset();

        join(config);
        QCOMPARE(m_server->stands().stands().size(), 1);
        QcArmourStand loaded = m_server->stands().stands().constFirst();
        loaded.id = saved.id;
        loaded.sinceHit = saved.sinceHit;
        QCOMPARE(loaded, saved);
    }

    void standsKeepWhatTheyWearThroughTheEntityFiles()
    {
        QcArmourStand stand;
        stand.motion.position = {1.5, 64.0, -3.5};
        stand.item = u"qtcraft:armor_stand"_s;
        stand.name = u"Tin"_s;
        stand.equipment.at(QcEquipmentSlot::Head) = {item("qtcraft:iron_helmet"), 1, 12};
        stand.held = {item("qtcraft:stick"), 1, 0};
        stand.pose.at(QcStandPose::Part::Head) = {10.0f, -20.0f, 5.0f};
        QcEntityRecord record;
        record.chunk = {0, -1};
        record.stands.append({stand, 0});
        const auto roundTrip = [this](const QcEntityRecord &written, QStringList &warnings) {
            QSet<QString> reported;
            return QcEntityCodec::decode(QcEntityCodec::encode(written, *m_content), written.chunk, *m_content, warnings, reported);
        };
        QStringList warnings;
        const auto read = roundTrip(record, warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QCOMPARE(read->stands.size(), 1);
        QCOMPARE(read->stands.first().entry, stand);

        // A cape the content no longer offers is dropped with a warning; the stand stays.
        record.stands.first().entry.cape = u"oldmod:cape"_s;
        QCOMPARE(roundTrip(record, warnings)->stands.first().entry, stand);
        QCOMPARE(warnings.size(), 1);

        // Armour that does not fit its slot any more is dropped with a warning.
        record.stands.first().entry = stand;
        record.stands.first().entry.equipment.at(QcEquipmentSlot::Feet) = {item("qtcraft:iron_helmet"), 1, 0};
        warnings.clear();
        QCOMPARE(roundTrip(record, warnings)->stands.first().entry, stand);
        QCOMPARE(warnings.size(), 1);
    }

private:
    QcChunkPos m_standChunk;

    // The bytes the entity region file at `path` holds for the stand's chunk, as they are, with the
    // checksum of its index entry (none when it holds nothing for it).
    QByteArray storedBlob(const QString &path) const
    {
        const QcResult<QcRegionFile::Index> index = QcRegionFile::readIndex(path, QcRegionFile::regionOf(m_standChunk), "QCER", 3);
        if (!index || !index->contains(m_standChunk)) {
            return {};
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(index->value(m_standChunk).offset)) {
            return {};
        }
        return file.read(index->value(m_standChunk).size) + " crc " + QByteArray::number(index->value(m_standChunk).crc);
    }

    // Saves a world with one stand and returns the path of its entity region file.
    QString saveWorldWithAStand(const QcServerConfig &config)
    {
        join(config);
        placeStand();
        const QcChunkPos chunk = QcPhysics::blockContaining(m_server->stands().stands().constFirst().motion.position).chunk();
        m_standChunk = chunk;
        [[maybe_unused]] const bool saved = m_server->save().has_value();
        m_client.reset();
        m_server.reset();
        const QcChunkPos region = QcWorldStorage::regionOf(chunk);
        return QDir(QDir(config.worldDirectory).filePath(u"entities"_s)).filePath(u"r.%1.%2.qen"_s.arg(region.x).arg(region.z));
    }

private slots:
    void aWorldWithADamagedEntityIndexIsRefused()
    {
        // Stands are entities like the others: a damaged index of their region file stops the world
        // from opening with a message, instead of losing them unnoticed (decisions E-366, E-373).
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        const QString path = saveWorldWithAStand(config);
        const QString name = QFileInfo(path).fileName();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        // The first index entry, behind magic, version, count and crc.
        file.seek(14);
        file.write("!!");
        file.close();

        {
            // The message names the file and the way out; the menu shows the same through damagedFile().
            QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            const QcStatus opened = server.openWorld();
            QVERIFY(!opened.has_value());
            QVERIFY2(opened.error().contains(name), qPrintable(opened.error()));
            QVERIFY2(opened.error().contains(u"out of the world folder opens the world without the creatures"_s), qPrintable(opened.error()));
            const QcServerPersistence::DamagedFile damaged = server.damagedFile();
            const bool entityPart = damaged.part == QcServerPersistence::DamagedPart::Entities;
            QVERIFY(entityPart);
            QCOMPARE(damaged.file, u"entities/"_s + name);
            QVERIFY(!server.worldTooOld());
        }

        // Moved away, the file no longer stops the world: it opens without the stand.
        QVERIFY(QFile::rename(file.fileName(), dir.filePath(name)));
        join(config);
        const bool undamaged = m_server->damagedFile().part == QcServerPersistence::DamagedPart::None;
        QVERIFY(undamaged);
        QVERIFY(m_server->stands().stands().isEmpty());
    }

    void aDamagedChunkPlaysAsGeneratedAndKeepsItsData()
    {
        // Damage inside a chunk's data is found when the chunk is read (decision E-373): the world
        // opens, the chunk plays as generated without what was kept of it, and its data on disk stays
        // as it is, so nothing is overwritten.
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        const QString path = saveWorldWithAStand(config);
        const QcResult<QcRegionFile::Index> index = QcRegionFile::readIndex(path, QcRegionFile::regionOf(m_standChunk), "QCER", 3);
        QVERIFY(index.has_value() && index->contains(m_standChunk));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        file.seek(index->value(m_standChunk).offset);
        file.write("!!");
        file.close();
        // The chunk's own bytes are compared: a save may rewrite the region for other chunks.
        const QByteArray before = storedBlob(path);
        QVERIFY(before.contains("!!"));

        join(config);
        QVERIFY(m_server->stands().stands().isEmpty());
        QVERIFY(m_server->save().has_value());
        QCOMPARE(storedBlob(path), before);

        // The operators are told, once; /regenerate gives the damaged data up and the chunk is
        // saved again (review of 31.3).
        ticks(1);
        pump();
        const auto told = [this] {
            return std::ranges::count_if(m_client->chatLines, [](const QString &line) { return line.contains(u"is damaged"_s); });
        };
        QCOMPARE(told(), 1);
        QVERIFY(m_server->world().isQuarantined(m_standChunk));
        chat(u"/regenerate %1 %2"_s.arg(m_standChunk.x * QcWorld::chunkSize).arg(m_standChunk.z * QcWorld::chunkSize));
        chat(u"/confirm"_s);
        QVERIFY(!m_server->world().isQuarantined(m_standChunk));
        QVERIFY(m_server->save().has_value());
        QVERIFY(storedBlob(path) != before);
        cleanup();
        join(config);
        ticks(1);
        pump();
        QVERIFY(m_server->world().chunk(m_standChunk));
        QVERIFY(!m_server->world().isQuarantined(m_standChunk));
        QCOMPARE(told(), 0);
    }

    void restoringMoreThanTheLimitCountsTheRest()
    {
        QcArmourStandManager manager;
        const QList<QcArmourStand> stands(QcArmourStandManager::maxStands + 7);
        QCOMPARE(manager.restore(stands), qsizetype(7));
        QCOMPARE(manager.stands().size(), qsizetype(QcArmourStandManager::maxStands));
        QCOMPARE(manager.restore(stands.mid(0, 3)), qsizetype(0));
    }

    void showcaseHasAStandForEverySet()
    {
        QCOMPARE(QcShowcaseArmourArea::sets(*m_content),
                 (QStringList{u"qtcraft:leather"_s, u"qtcraft:golden"_s, u"qtcraft:iron"_s, u"qtcraft:diamond"_s}));

        QStringList warnings;
        const QcShowcasePlan plan = QcShowcasePlanner(*m_content, &warnings).build();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QStringList equipped;
        for (const QcWorldSetup::Stand &stand : plan.setup.stands) {
            for (const QString &property : stand.properties) {
                if (property.startsWith(u"equip qtcraft:"_s) && property.count(u'_') == 0) {
                    equipped.append(property.mid(6));
                }
            }
        }
        const QStringList sets = QcShowcaseArmourArea::sets(*m_content);
        for (const QString &set : sets) {
            QVERIFY2(equipped.contains(set), qPrintable(set));
        }
        // The pose demo brings its own stands.
        QVERIFY(plan.setup.stands.size() >= QcShowcaseArmourArea::sets(*m_content).size() + 5);
        QVERIFY(std::ranges::any_of(plan.areas, [](const auto &area) { return area.name == u"armour"_s; }));
    }
};

QTEST_GUILESS_MAIN(TestArmourStands)
#include "test_armour_stands.moc"
