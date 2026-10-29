// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcChunkExtras.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/spawners/QcSpawnerRules.h"
#include "server/storage/QcBlockEntityCodec.h"
#include "server/storage/QcSaveFile.h"
#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>

#include <atomic>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The test terrain with a chest (storage, no loot) deep in every chunk; it counts how often the
// server asks it for the furnishing of the chunk `watched`.
class ChestTerrain : public QcTestTerrainGenerator
{
public:
    struct Asked
    {
        QcChunkPos watched;
        std::atomic<int> count{0};
    };

    ChestTerrain(const std::shared_ptr<const QcContentRegistry> &content, std::shared_ptr<Asked> asked)
        : QcTestTerrainGenerator(content)
        , m_chest(*content->blockId(u"qtcraft:chest"_s))
        , m_asked(std::move(asked))
    {
    }

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override
    {
        std::shared_ptr<QcChunk> chunk = QcTestTerrainGenerator::generate(pos);
        chunk->setBlock(8, 2, 8, m_chest);
        return chunk;
    }

    QcChunkFurnishing furnishingIn(QcChunkPos pos) const override
    {
        if (pos == m_asked->watched) {
            ++m_asked->count;
        }
        return {};
    }

private:
    QcBlockId m_chest;
    std::shared_ptr<Asked> m_asked;
};

} // namespace

// Monster spawners (decision E-520): a spawner works only while a player is near, waits its delay,
// spawns up to `count` creatures of its kind on dark cells with ground in its area, stops at
// `maxNearby` of them and at the world's limit for spawned creatures; /setblock names its kind; the
// kind, delay and confined flag are saved; breaking it gives experience and no item. Ticks are
// stepped by hand.
class TestSpawners : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr int arena = 14;

    int zombieType() const { return *m_content->mobIndex(u"qtcraft:zombie"_s); }
    QcServerClient &player() { return *m_server->entities().clients().front(); }
    QcBlockPos spawnerPos() const { return m_feet.offset(0, 0, 8); }
    const QcSpawners::Entry *spawner() const { return m_server->world().spawners().at(spawnerPos()); }

    QList<const QcCreature *> spawned() const
    {
        QList<const QcCreature *> found;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (mob.spawner) {
                found.append(&mob);
            }
        }
        return found;
    }

    // A stone floor and open air above it, no creatures, at night, in creative (zombies leave the
    // player alone, nothing spawns naturally).
    void joinArena(const QcServerConfig &config = {})
    {
        join(config);
        chat(u"/gamemode creative"_s);
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule daylightCycle false"_s);
        chat(u"/time set 18000"_s);
        loadAround(m_feet, 2);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 6; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->mobs().removeAll();
    }

    void placeSpawner(const QString &mob = u"zombie"_s)
    {
        const QcBlockPos at = spawnerPos();
        chat(u"/setblock %1 %2 %3 spawner %4"_s.arg(at.x).arg(at.y).arg(at.z).arg(mob));
        ticks(1);
    }

    // Ticks until the spawner's first cycle has spawned, at most `limit`.
    int ticksUntilSpawned(int limit)
    {
        for (int tick = 1; tick <= limit; ++tick) {
            ticks(1);
            if (!spawned().isEmpty()) {
                return tick;
            }
        }
        return -1;
    }

    // Ticks in which a spawner whose area has a single fitting cell surely hits it (4 of 243 cells
    // drawn per tick while it finds none).
    static constexpr int oneCellTicks = 2000;

    // Stone fills the spawn area at the spawner's level and the level above, but for the zombie's two
    // cells at `cell` (the spawner's level): the only cell of the area that fits.
    void oneCellArea(QcBlockPos cell)
    {
        for (int dx = -4; dx <= 4; ++dx) {
            for (int dz = -4; dz <= 4; ++dz) {
                for (int dy = 0; dy <= 1; ++dy) {
                    const QcBlockPos at = spawnerPos().offset(dx, dy, dz);
                    if (at != spawnerPos() && at != cell && at != cell.offset(0, 1, 0)) {
                        m_server->world().setBlock(at, id("qtcraft:stone"));
                    }
                }
            }
        }
    }

    // The rail of the first chest minecart of a mine shaft in the regions east of the origin.
    static QcBlockPos mineshaftCartRail(const QcTerrainGenerator &terrain)
    {
        for (int r = 0; r <= 4; ++r) {
            if (const std::shared_ptr<const QcMineshaftPlan> plan = terrain.mineshafts().planOf(r, 0); plan && !plan->carts.isEmpty()) {
                return plan->carts.constFirst();
            }
        }
        return {};
    }

    QList<QcMinecart *> cartsAt(QcBlockPos rail)
    {
        QList<QcMinecart *> found;
        for (QcMinecart &cart : m_server->entities().minecarts().editableAll()) {
            if (QcPhysics::blockContaining(cart.motion.position) == rail) {
                found.append(&cart);
            }
        }
        return found;
    }

    // A dungeon of the terrain with chests for seed 381.
    static std::shared_ptr<const QcDungeonPlan> dungeonWithChests(const QcTerrainGenerator &terrain)
    {
        std::shared_ptr<const QcDungeonPlan> plan;
        for (int r = 2; r <= 6 && !plan; ++r) {
            plan = terrain.dungeons().planOf(r, 0);
            plan = plan && !plan->chests.isEmpty() ? plan : nullptr;
        }
        return plan;
    }

    void teleport(const QcVec3 &to)
    {
        chat(u"/tp %1 %2 %3"_s.arg(to.x).arg(to.y).arg(to.z));
        ticks(1);
    }

    // A blob with the spawners table (10) by hand, one entry of version 2: the fields of version 1 and two
    // bytes more.
    static QByteArray futureBlob(QcBlockPos pos)
    {
        const auto stream = [](QByteArray &bytes) {
            auto out = std::make_unique<QDataStream>(&bytes, QIODevice::WriteOnly);
            out->setVersion(QcSaveFile::streamVersion);
            return out;
        };
        QByteArray entry;
        *stream(entry) << static_cast<qint32>(pos.x) << static_cast<qint32>(pos.y) << static_cast<qint32>(pos.z) << u"qtcraft:zombie"_s
            << quint32{9} << quint8{1} << quint16{0xbeef};
        QByteArray payload;
        {
            const auto out = stream(payload);
            *out << quint32{1} << quint16{2} << static_cast<quint32>(entry.size());
            out->writeRawData(entry.constData(), static_cast<int>(entry.size()));
        }
        QByteArray blob;
        const auto out = stream(blob);
        *out << quint16{1} << static_cast<quint16>(QcBlockEntityRecord::Table::Spawners) << quint16{1} << static_cast<quint32>(payload.size());
        out->writeRawData(payload.constData(), static_cast<int>(payload.size()));
        return blob;
    }

private slots:
    void spawnsItsKindInItsAreaAfterItsDelay()
    {
        joinArena();
        placeSpawner();
        QVERIFY(spawner());
        QCOMPARE(spawner()->mob, u"qtcraft:zombie"_s);
        QCOMPARE(spawner()->delay, QcSpawners::firstDelay - 1);
        // A confined spawner (the showcase's) gives its creatures the flag.
        QcSpawners::Entry confined = *spawner();
        confined.confined = true;
        QVERIFY(m_server->world().spawners().set(confined));

        // Nothing before the first delay ran out, then up to `count` (4) in one cycle.
        const int tick = ticksUntilSpawned(400);
        QVERIFY2(tick >= int(QcSpawners::firstDelay) - 1, qPrintable(QString::number(tick)));
        const QList<const QcCreature *> first = spawned();
        QVERIFY(!first.isEmpty() && first.size() <= 4);
        for (const QcCreature *mob : first) {
            QCOMPARE(mob->type, zombieType());
            QCOMPARE(*mob->spawner, spawnerPos());
            QVERIFY(!mob->persistent);
            QVERIFY(mob->confined);
            const QcBlockPos feet = QcPhysics::blockContaining(mob->motion.position);
            QVERIFY(std::abs(feet.x - spawnerPos().x) <= 4 && std::abs(feet.z - spawnerPos().z) <= 4);
            QCOMPARE(feet.y, spawnerPos().y);
        }

        // Then a delay of 10 to 40 seconds.
        QVERIFY(spawner()->delay >= 10 * 20 - 1 && spawner()->delay <= 40 * 20);
    }

    void waitsWhileNoPlayerIsNear()
    {
        joinArena();
        teleport({m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z - 9.5});
        placeSpawner();
        const quint32 before = spawner()->delay;
        ticks(60);
        QCOMPARE(spawner()->delay, before);
        QVERIFY(spawned().isEmpty());

        // Within 16 blocks it counts down again.
        teleport({m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z - 6.5});
        ticks(5);
        QVERIFY(spawner()->delay < before);
    }

    void spawnsOnlyWhereItIsDark()
    {
        joinArena();
        // Lanterns over the whole area and its edge: every cell is lit brighter than 11.
        for (int dx = -6; dx <= 6; dx += 2) {
            for (int dz = -6; dz <= 6; dz += 2) {
                m_server->world().setBlock(spawnerPos().offset(dx, 1, dz), id("qtcraft:lantern"));
            }
        }
        placeSpawner();
        QCOMPARE(ticksUntilSpawned(200), -1);
        // A cycle without a fitting cell tries again in the next tick.
        QCOMPARE(spawner()->delay, quint32(0));
    }

    void stopsAtMaxNearby()
    {
        joinArena();
        placeSpawner();
        for (int i = 0; i < 6; ++i) {
            m_server->mobs().spawn(zombieType(),
                {spawnerPos().x + 0.5 + (i % 3), static_cast<double>(spawnerPos().y), spawnerPos().z + 2.5}, m_server->entities().newId());
        }
        ticks(static_cast<int>(QcSpawners::firstDelay) + 1);
        QVERIFY(spawned().isEmpty());
        // Its area was full: it waits a whole delay.
        QVERIFY(spawner()->delay >= 10 * 20 - 2);
    }

    void stopsAtTheWorldsLimit()
    {
        QcServerConfig config;
        config.limits.spawnerCreatures = 2;
        joinArena(config);
        placeSpawner();
        for (int cycle = 0; cycle < 6; ++cycle) {
            ticks(static_cast<int>(spawner()->delay) + 2);
        }
        QVERIFY(!spawned().isEmpty());
        QVERIFY(spawned().size() <= 2);
    }

    void setblockNamesTheKind()
    {
        joinArena();
        placeSpawner(u"skeleton"_s);
        QCOMPARE(spawner()->mob, u"qtcraft:skeleton"_s);
        placeSpawner(u"no_such_creature"_s);
        QCOMPARE(spawner()->mob, u"qtcraft:skeleton"_s);
        // Only a spawner takes a kind.
        chat(u"/setblock %1 %2 %3 stone zombie"_s.arg(spawnerPos().x).arg(spawnerPos().y).arg(spawnerPos().z));
        ticks(1);
        QCOMPARE(*m_server->world().blockAt(spawnerPos()), id("qtcraft:spawner"));

        // Without a kind it waits as an empty cage; breaking it forgets it.
        m_server->world().setBlock(m_feet.offset(3, 0, 3), id("qtcraft:spawner"));
        QCOMPARE(m_server->world().spawners().at(m_feet.offset(3, 0, 3))->mob, QString());
        m_server->world().setBlock(m_feet.offset(3, 0, 3), qcAirBlockId);
        QVERIFY(!m_server->world().spawners().at(m_feet.offset(3, 0, 3)));
    }

    void breakingGivesExperienceAndNoItem()
    {
        join();
        chat(u"/give iron_pickaxe"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:iron_pickaxe"))});
        const QcBlockPos pos = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(pos, id("qtcraft:spawner"));
        m_server->items().clear();
        m_server->orbs().clear();
        const QcItemDefinition &pickaxe = m_content->item(item("qtcraft:iron_pickaxe"));
        send(QcMsg::StartMining{pos});
        ticks(static_cast<int>(std::ceil(QcMining::breakSeconds(m_content->block(id("qtcraft:spawner")), &pickaxe) * QcPhysics::tickRate)) + 1);
        send(QcMsg::FinishMining{pos});
        ticks(1);
        QCOMPARE(*m_server->world().blockAt(pos), qcAirBlockId);
        QVERIFY(m_server->items().items().isEmpty() && m_server->items().queued().isEmpty());
        const qint64 points = m_server->orbs().totalPoints() + player().experience.total();
        QVERIFY2(points >= 15 && points <= 43, qPrintable(QString::number(points)));
    }

    void entriesAreSavedWithUnknownFieldsKept()
    {
        QcBlockEntityRecord record;
        record.chunk = {0, 0};
        record.spawners = {{{3, 20, 5}, u"qtcraft:zombie"_s, 123, false}, {{4, 21, 5}, u"mod:ghost"_s, 7, true}};
        record.furnished = true;
        const QByteArray blob = QcBlockEntityCodec::encode(record, *m_content);
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = QcBlockEntityCodec::decode(blob, {0, 0}, *m_content, warnings);
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->spawners, record.spawners);
        QVERIFY(read->furnished);

        // An entry of a later version (2) with a field this one does not know comes back unchanged.
        const QcBlockPos pos{6, 30, 2};
        const QByteArray future = futureBlob(pos);
        const QcResult<QcBlockEntityRecord> later = QcBlockEntityCodec::decode(future, {0, 0}, *m_content, warnings);
        QVERIFY2(later.has_value(), qPrintable(later ? QString() : later.error()));
        QCOMPARE(later->spawners.size(), 1);
        QCOMPARE(later->spawners[0], (QcSpawners::Entry{pos, u"qtcraft:zombie"_s, 9, true}));
        QCOMPARE(later->kept.entries.size(), 1);
        QCOMPARE(QcBlockEntityCodec::encode(*later, *m_content), future);
    }

    void spawnersComeBackWithTheirChunk()
    {
        m_unloadChunks = true;
        joinArena();
        const QcBlockPos far = m_feet.offset(160, 0, 0);
        m_server->world().loadChunk(far.chunk());
        m_server->world().setBlock(far, id("qtcraft:spawner"));
        QVERIFY(m_server->world().spawners().set({far, u"qtcraft:spider"_s, 77, true}));
        // The player is far away: the chunk leaves the world, then comes back.
        ticks(200);
        QVERIFY(!m_server->world().chunk(far.chunk()));
        m_server->world().loadChunk(far.chunk());
        QCOMPARE(m_server->world().blockAt(far), std::optional<QcBlockId>(id("qtcraft:spawner")));
        const QcSpawners::Entry *back = m_server->world().spawners().at(far);
        QVERIFY(back);
        QCOMPARE(back->mob, u"qtcraft:spider"_s);
        QCOMPARE(back->delay, quint32(77));
        QVERIFY(back->confined);
    }

    // Block light 11 at the only fitting cell lets it spawn, 12 does not (a torch, 14, three or two
    // cells away through the air above).
    void spawnsAtBlockLightElevenNotTwelve()
    {
        for (const int distance : {3, 2}) {
            joinArena();
            const QcBlockPos cell = spawnerPos().offset(2, 0, 0);
            oneCellArea(cell);
            m_server->world().setBlock(cell.offset(distance - 2, 2, 0), id("qtcraft:torch"));
            QCOMPARE(m_server->world().blockLight(cell), 14 - distance);
            placeSpawner();
            const int tick = ticksUntilSpawned(oneCellTicks);
            QVERIFY2(distance == 3 ? tick > 0 : tick == -1, qPrintable(u"%1: %2"_s.arg(distance).arg(tick)));
            if (tick > 0) {
                QCOMPARE(QcPhysics::blockContaining(spawned().constFirst()->motion.position), cell);
            }
        }
    }

    // Sky light does not count: at noon under the open sky it spawns.
    void skyLightIsIgnoredInDaylight()
    {
        joinArena();
        chat(u"/time set 6000"_s);
        const QcBlockPos cell = spawnerPos().offset(-3, 0, 1);
        oneCellArea(cell);
        QVERIFY(m_server->world().skyLight(cell.offset(0, 1, 0)) >= 12);
        placeSpawner();
        QVERIFY(ticksUntilSpawned(oneCellTicks) > 0);
    }

    // The maximum counts its kind within 4 blocks of the spawner on every axis, apart from the spawn
    // area: six zombies 4 above it stop it, six 5 to the side do not.
    void nearbyCountsTheCubeAroundTheSpawner()
    {
        for (const bool inside : {true, false}) {
            joinArena();
            placeSpawner();
            for (int i = 0; i < 6; ++i) {
                const QcVec3 at = inside ? QcVec3{spawnerPos().x + 0.5 + (i % 3), static_cast<double>(spawnerPos().y + 4), spawnerPos().z + 0.5}
                                         : QcVec3{spawnerPos().x + 5.5, static_cast<double>(spawnerPos().y), spawnerPos().z - 2.5 + i % 3};
                m_server->world().setBlock(QcPhysics::blockContaining(at).offset(0, -1, 0), id("qtcraft:stone"));
                m_server->mobs().spawn(zombieType(), at, m_server->entities().newId());
            }
            ticks(static_cast<int>(QcSpawners::firstDelay) + 1);
            QVERIFY2(spawned().isEmpty() == inside, qPrintable(QString::number(spawned().size())));
        }
    }

    // Villagers and fish are refused by /setblock.
    void refusesVillagers()
    {
        joinArena();
        placeSpawner(u"zombie"_s);
        placeSpawner(u"villager"_s);
        QCOMPARE(spawner()->mob, u"qtcraft:zombie"_s);
        placeSpawner(u"cod"_s);
        QCOMPARE(spawner()->mob, u"qtcraft:zombie"_s);
    }

    // The client shows a spawner's kind while its chunk is loaded: -1 removes it, ChunkUnload and a
    // new ChunkData of the chunk drop it; the message keeps its fields through the codec.
    void clientShowsTheKindsOfItsChunks()
    {
        const QcMsg::SpawnerDisplay display{{17, 30, -5}, 3};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage(display)), QcProtocol::Peer::Server);
        QVERIFY(decoded && std::holds_alternative<QcMsg::SpawnerDisplay>(*decoded));
        QCOMPARE(std::get<QcMsg::SpawnerDisplay>(*decoded).pos, display.pos);
        QCOMPARE(std::get<QcMsg::SpawnerDisplay>(*decoded).mob, 3);
        const QcResult<QcMessage> none = QcProtocol::decode(QcProtocol::encode(QcMessage(QcMsg::SpawnerDisplay{{1, 2, 3}, -1})), QcProtocol::Peer::Server);
        QVERIFY(none && std::get<QcMsg::SpawnerDisplay>(*none).mob == -1);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(QcMsg::SpawnerDisplay{{1, 2, 3}, -2})), QcProtocol::Peer::Server));

        QcChunkExtras extras;
        extras.setSpawner(display);
        extras.setSpawner({{18, 30, -5}, 4});
        extras.setSpawner({{40, 30, 40}, 5});
        QCOMPARE(extras.spawners().size(), 3);
        extras.setSpawner({{18, 30, -5}, -1});
        QCOMPARE(extras.spawners().size(), 2);
        extras.removeChunk(display.pos.chunk());
        QCOMPARE(extras.spawners().keys(), QList<QcBlockPos>{QcBlockPos(40, 30, 40)});
        extras.chunkArrived(QcBlockPos{40, 30, 40}.chunk());
        QVERIFY(extras.spawners().isEmpty());
    }

    // /regenerate gives a dungeon's spawner its kind and the chests their loot again; a spawner set
    // by hand in a regenerated chunk is gone.
    void regenerateFurnishesAgain()
    {
        auto terrain = std::make_unique<QcTerrainGenerator>(m_content, 381);
        const std::shared_ptr<const QcDungeonPlan> plan = dungeonWithChests(*terrain);
        QVERIFY(plan);
        join({}, std::move(terrain));
        const QcBlockPos chest = plan->chests.constFirst().at;
        m_server->world().loadChunk(plan->spawner.chunk());
        m_server->world().loadChunk(chest.chunk());
        const QString other = plan->mob == u"qtcraft:spider"_s ? u"qtcraft:zombie"_s : u"qtcraft:spider"_s;
        QVERIFY(m_server->world().spawners().set({plan->spawner, other, 5, true}));
        std::ranges::fill(m_server->blockEntities().container(chest)->stacks, QcItemStack{});
        const QcBlockPos extra = plan->spawner.offset(0, 1, 0);
        m_server->world().setBlock(extra, id("qtcraft:spawner"));
        QVERIFY(m_server->world().spawners().at(extra));

        for (const QcChunkPos &pos : {plan->spawner.chunk(), chest.chunk()}) {
            chat(u"/regenerate %1 %2"_s.arg(pos.x * QcWorld::chunkSize).arg(pos.z * QcWorld::chunkSize));
            chat(u"/confirm"_s);
        }
        QVERIFY(m_server->world().spawners().at(plan->spawner));
        QCOMPARE(m_server->world().spawners().at(plan->spawner)->mob, plan->mob);
        QVERIFY(!m_server->world().spawners().at(plan->spawner)->confined);
        QVERIFY(!m_server->world().spawners().at(extra));
        QVERIFY(m_server->world().blockEntityExtras().isFurnished(chest.chunk()));
        QcBlockEntities::Container *container = m_server->blockEntities().container(chest);
        QVERIFY(container);
        QVERIFY(std::ranges::any_of(container->stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); }));
    }

    // Debug-relative cost of spawners in the server tick (stage 38.1, docs/Performance-Verlauf.md):
    // 0 against 120 spawners near the player, with the limit of spawned creatures at 0 so only the
    // spawners' own work counts (their cycles find the limit reached). Printed, not checked.
    void measureTickCost()
    {
        QcServerConfig config;
        config.limits.spawnerCreatures = 0;
        joinArena(config);
        const auto perTick = [this] {
            QElapsedTimer timer;
            timer.start();
            ticks(400);
            return static_cast<double>(timer.nsecsElapsed()) / 400.0 / 1e6;
        };
        const double before = perTick();
        for (int i = 0; i < 120; ++i) {
            m_server->world().setBlock(m_feet.offset(-10 + i % 20, 0, -10 + i / 20 * 4), id("qtcraft:spawner"));
            m_server->world().spawners().set({m_feet.offset(-10 + i % 20, 0, -10 + i / 20 * 4), u"qtcraft:zombie"_s, 0, false});
        }
        QCOMPARE(m_server->world().spawners().size(), 120);
        const double with = perTick();
        const double again = perTick();
        qInfo().noquote() << u"ms per tick: no spawners %1, 120 spawners %2 / %3"_s.arg(before, 0, 'f', 3).arg(with, 0, 'f', 3).arg(again, 0, 'f', 3);
    }

    // A dungeon's chunks get the spawner's kind and the chests' loot once (decision E-520): a chest
    // emptied since stays empty when its chunk comes back.
    void dungeonChunksAreFurnishedOnce()
    {
        m_unloadChunks = true;
        auto terrain = std::make_unique<QcTerrainGenerator>(m_content, 381);
        const std::shared_ptr<const QcDungeonPlan> plan = dungeonWithChests(*terrain);
        QVERIFY(plan);
        join({}, std::move(terrain));
        const QcBlockPos chest = plan->chests.constFirst().at;
        m_server->world().loadChunk(plan->spawner.chunk());
        m_server->world().loadChunk(chest.chunk());
        QCOMPARE(m_server->world().spawners().at(plan->spawner)->mob, plan->mob);
        QVERIFY(m_server->world().blockEntityExtras().isFurnished(chest.chunk()));
        QcBlockEntities::Container *container = m_server->blockEntities().container(chest);
        QVERIFY(container);
        QVERIFY(std::ranges::any_of(container->stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); }));
        std::ranges::fill(container->stacks, QcItemStack{});

        // Far from the player the chunks leave the world and come back.
        ticks(200);
        QVERIFY(!m_server->world().chunk(chest.chunk()));
        m_server->world().loadChunk(chest.chunk());
        m_server->world().loadChunk(plan->spawner.chunk());
        QVERIFY(m_server->world().blockEntityExtras().isFurnished(chest.chunk()));
        container = m_server->blockEntities().container(chest);
        QVERIFY(container);
        QVERIFY(std::ranges::all_of(container->stacks, [](const QcItemStack &stack) { return stack.isEmpty(); }));
        QCOMPARE(m_server->world().spawners().at(plan->spawner)->mob, plan->mob);
    }

    // The generator is asked for a chunk's furnishing once (it may plan regions, on the tick thread):
    // a chunk it has nothing for is marked furnished, and blocks read back from the store are not
    // furnished again.
    void chunksWithoutLootAreAskedOnce()
    {
        m_unloadChunks = true;
        auto asked = std::make_shared<ChestTerrain::Asked>();
        join({}, std::make_unique<ChestTerrain>(m_content, asked));
        const QcChunkPos far{m_feet.chunk().x + 20, m_feet.chunk().z};
        asked->watched = far;
        asked->count = 0;
        m_server->world().loadChunk(far);
        QCOMPARE(asked->count.load(), 1);
        QVERIFY(m_server->world().blockEntityExtras().isFurnished(far));

        // Unchanged it leaves and is generated again with its kept mark.
        ticks(200);
        QVERIFY(!m_server->world().chunk(far));
        m_server->world().loadChunk(far);
        QCOMPARE(asked->count.load(), 1);

        // Changed it leaves and its blocks are read back.
        m_server->world().setBlock(QcBlockPos{far.x * QcWorld::chunkSize + 3, 2, far.z * QcWorld::chunkSize + 3}, id("qtcraft:chest"));
        ticks(200);
        QVERIFY(!m_server->world().chunk(far));
        m_server->world().loadChunk(far);
        QCOMPARE(asked->count.load(), 1);
        QVERIFY(m_server->world().blockEntityExtras().isFurnished(far));
    }

    // A mine shaft's chest minecart is put on its rail with loot once (decision E-570): it is saved
    // with its chunk, and a cart emptied since comes back empty, not a second time.
    void mineshaftCartsAreFurnishedOnce()
    {
        m_unloadChunks = true;
        auto terrain = std::make_unique<QcTerrainGenerator>(m_content, 381);
        const QcBlockPos rail = mineshaftCartRail(*terrain);
        QVERIFY(rail != QcBlockPos{});
        join({}, std::move(terrain));
        const auto cartsAtRail = [this, rail] { return cartsAt(rail); };
        m_server->world().loadChunk(rail.chunk());
        QVERIFY(m_server->world().blockEntityExtras().isFurnished(rail.chunk()));
        QCOMPARE(cartsAtRail().size(), 1);
        QcMinecart &cart = *cartsAtRail().constFirst();
        QCOMPARE(cart.item, u"qtcraft:chest_minecart"_s);
        QVERIFY(std::ranges::any_of(cart.stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); }));
        std::ranges::fill(cart.stacks, QcItemStack{});

        // Far from the player the chunk leaves the world and comes back with its cart.
        ticks(200);
        QVERIFY(!m_server->world().chunk(rail.chunk()));
        QVERIFY(cartsAtRail().isEmpty());
        m_server->world().loadChunk(rail.chunk());
        QCOMPARE(cartsAtRail().size(), 1);
        QVERIFY(std::ranges::all_of(cartsAtRail().constFirst()->stacks, [](const QcItemStack &stack) { return stack.isEmpty(); }));
    }

    // /regenerate puts a mine shaft's chest minecart on its rail again, filled, instead of a
    // second one beside the old (the old carts go with the old blocks).
    void regenerateKeepsOneMineshaftCart()
    {
        auto terrain = std::make_unique<QcTerrainGenerator>(m_content, 381);
        const QcBlockPos rail = mineshaftCartRail(*terrain);
        QVERIFY(rail != QcBlockPos{});
        join({}, std::move(terrain));
        m_server->world().loadChunk(rail.chunk());
        QCOMPARE(cartsAt(rail).size(), 1);
        std::ranges::fill(cartsAt(rail).constFirst()->stacks, QcItemStack{});

        chat(u"/regenerate %1 %2"_s.arg(rail.x).arg(rail.z));
        chat(u"/confirm"_s);
        QCOMPARE(cartsAt(rail).size(), 1);
        QVERIFY(std::ranges::any_of(cartsAt(rail).constFirst()->stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); }));
    }

    // Debug-relative cost of furnishing arriving chunks on the tick thread (decision E-520), printed,
    // not checked: asking the dungeons of every chunk with the regions in reach (as before) against
    // asking only chunks with a spawner, storage or a rail, and only the regions they overlap.
    void measureFurnishingCost()
    {
        auto terrain = std::make_unique<QcTerrainGenerator>(m_content, 381);
        const QcTerrainGenerator &generator = *terrain;
        join({}, std::move(terrain));
        QList<QcChunkPos> chunks;
        for (int cz = -5; cz < 5; ++cz) {
            for (int cx = 0; cx < 12; ++cx) {
                m_server->world().loadChunk({cx, cz});
                chunks.append({cx, cz});
            }
        }
        const QcSectionMarks &marks = m_server->world().marks();
        const auto time = [&](auto &&lookup) {
            QElapsedTimer timer;
            timer.start();
            qsizetype found = 0;
            for (int round = 0; round < 20; ++round) {
                for (const QcChunkPos &pos : std::as_const(chunks)) {
                    found += lookup(pos);
                }
            }
            return std::pair{static_cast<double>(timer.nsecsElapsed()) / 1e6 / 20.0, found};
        };
        const auto every = time([&](QcChunkPos pos) { return generator.dungeons().plansTouching(pos).size(); });
        const auto marked = time([&](QcChunkPos pos) -> qsizetype {
            if (marks.sectionsOf(pos, QcBlockTrait::Spawner) == 0 && marks.sectionsOf(pos, QcBlockTrait::BlockEntity) == 0
                && marks.sectionsOf(pos, QcBlockTrait::Rail) == 0) {
                return 0;
            }
            return generator.furnishingIn(pos).spawners.size() + 1;
        });
        qInfo().noquote() << u"ms for %1 arriving chunks: every chunk %2, marked chunks only %3 (%4 asked)"_s.arg(chunks.size()).arg(every.first, 0, 'f', 3).arg(marked.first, 0, 'f', 3).arg(marked.second / 20);
    }
};

QTEST_GUILESS_MAIN(TestSpawners)
#include "test_spawners.moc"
