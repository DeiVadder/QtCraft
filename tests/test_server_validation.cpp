#include "QcBaseContent.h"
#include "QcConnection.h"
#include "QcInProcessTransport.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcServer.h"
#include "QcTestTerrainGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>

#include <limits>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

// A raw protocol client: sends exactly what a test wants and records everything the server says.
class RawClient
{
public:
    explicit RawClient(std::unique_ptr<QcTransport> transport)
        : connection(std::move(transport), QcProtocol::Peer::Server, [this](QcMessage &&message) { received(std::move(message)); })
    {
    }

    void received(QcMessage &&message)
    {
        if (const auto *state = std::get_if<QcMsg::PlayerState>(&message)) {
            motion = state->motion;
        } else if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
            stacks = inventory->stacks;
            craftingGrid = inventory->craftingGrid;
            craftingSize = inventory->craftingSize;
            craftingResult = inventory->craftingResult;
        } else if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
            chatLines.append(chat->text);
        } else if (const auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
            this->health = health->health;
            dead = health->dead;
        } else if (const auto *open = std::get_if<QcMsg::OpenCrafting>(&message)) {
            openedCrafting = open->size;
        } else if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
            entities.insert(spawn->id, spawn->mobType);
            if (spawn->mobType == QcMsg::EntitySpawn::itemType) {
                items.insert(spawn->id, spawn->item);
            }
            spawns.insert(spawn->id, *spawn);
        } else if (const auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
            entities.remove(removed->id);
            items.remove(removed->id);
            if (removed->died) {
                died.insert(removed->id);
            }
        } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
            for (const QcMsg::EntityMove &move : moves->moves) {
                flags[move.id] |= move.flags;
                if (move.hurt) {
                    hurt.insert(move.id);
                }
            }
        }
    }

    QcConnection connection;
    QcEntityMotion motion;
    QList<QcItemStack> stacks;
    QList<QcItemStack> craftingGrid;
    qint32 craftingSize = 0;
    QcItemStack craftingResult;
    qint32 openedCrafting = 0;
    QStringList chatLines;
    double health = 0.0;
    bool dead = false;
    QHash<quint32, qint32> entities;
    // Item entities the client was told about, with their latest stack.
    QHash<quint32, QcItemStack> items;
    // Latest spawn message per entity, entities reported dead, and all move flags seen per entity.
    QHash<quint32, QcMsg::EntitySpawn> spawns;
    QSet<quint32> died;
    QHash<quint32, quint8> flags;
    // Entities reported hurt in any move.
    QSet<quint32> hurt;
};

void pump()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

} // namespace

// Checks that the server refuses what a client must not be able to do, with raw protocol messages.
class TestServerValidation : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();
    std::unique_ptr<QcServer> m_server;
    std::unique_ptr<RawClient> m_client;
    QcBlockPos m_feet;
    quint32 m_sequence = 1000;

    QcBlockId id(const char *name) const { return *m_content->blockId(QString::fromLatin1(name)); }
    QcItemId item(const char *name) const { return *m_content->itemId(QString::fromLatin1(name)); }

    void ticks(int count)
    {
        for (int i = 0; i < count; ++i) {
            m_server->tick();
            pump();
        }
    }

    void join(QcServerConfig config = {})
    {
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->connection.send(QcMsg::Hello{QcProtocol::version, u"Raw"_s, {}});
        pump();
        ticks(2);
        const QcVec3 p = m_client->motion.position;
        m_feet = {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        // The spawn lies at a chunk corner; generate the neighbours now so the whole stage exists.
        for (int cx = -1; cx <= 1; ++cx) {
            for (int cz = -1; cz <= 1; ++cz) {
                m_server->world().loadChunk({m_feet.chunk().x + cx, m_feet.chunk().z + cz});
            }
        }
        // A flat 7 x 7 stage around the player, independent of the generated hills.
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    void chat(const QString &text)
    {
        m_client->connection.send(QcMsg::ChatInput{text});
        pump();
    }

    int slotOf(QcItemId wanted) const
    {
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            if (m_client->stacks[slot].item == wanted && !m_client->stacks[slot].isEmpty()) {
                return slot;
            }
        }
        return -1;
    }

    void send(const QcMessage &message)
    {
        m_client->connection.send(message);
        pump();
        ticks(1);
    }

    // Lets the player stand still for some ticks; the server moves players only on input.
    void idle(int count)
    {
        for (int i = 0; i < count; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            m_client->connection.send(QcMsg::PlayerInput{input});
            pump();
            ticks(1);
        }
    }

    // Drops the player down a 10 m shaft, which costs 7 health.
    void fallDownShaft()
    {
        for (int dy = -1; dy >= -10; --dy) {
            m_server->world().setBlock(m_feet.offset(0, dy, 0), qcAirBlockId);
        }
        m_server->world().setBlock(m_feet.offset(0, -11, 0), id("qtcraft:stone"));
        for (quint32 sequence = 1; sequence <= 60; ++sequence) {
            QcPlayerInput input;
            input.sequence = sequence;
            m_client->connection.send(QcMsg::PlayerInput{input});
            pump();
            ticks(1);
        }
    }

    int countOf(QcItemId wanted) const
    {
        int total = 0;
        for (const QcItemStack &stack : std::as_const(m_client->stacks)) {
            total += stack.item == wanted ? stack.count : 0;
        }
        return total;
    }

    int droppedCount(QcItemId wanted) const
    {
        int total = 0;
        for (const QcItemEntityManager::Item &dropped : m_server->items().items()) {
            total += dropped.stack.item == wanted ? dropped.stack.count : 0;
        }
        return total;
    }

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

    // Which other chunks of the stage around the origin count as modified depends on generation
    // timing, so tests only look for the region of the block they changed.
    static QString regionFile(const QString &world, QcBlockPos pos)
    {
        const QcChunkPos region = QcWorldStorage::regionOf(pos.chunk());
        return QDir(world).filePath(u"regions/r.%1.%2.qcr"_s.arg(region.x).arg(region.z));
    }

    void cheatCommandsCanBeDisabled_data()
    {
        QTest::addColumn<QString>("command");
        QTest::newRow("give") << u"/give glass 1"_s;
        QTest::newRow("time") << u"/time set night"_s;
        QTest::newRow("summon") << u"/summon zombie"_s;
        QTest::newRow("kill") << u"/kill"_s;
        QTest::newRow("heal") << u"/heal"_s;
    }

    void cheatCommandsCanBeDisabled()
    {
        QFETCH(QString, command);
        QcServerConfig config;
        config.allowCheats = false;
        join(config);
        const double time = m_server->worldTime();
        chat(command);
        QVERIFY(m_client->chatLines.constLast().contains(u"disabled"_s));
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

    void giveRejectsBadCountsAndCanBeDisabled()
    {
        join();
        chat(u"/give glass -5"_s);
        chat(u"/give glass 999999"_s);
        chat(u"/give no_such_item"_s);
        QCOMPARE(countOf(item("qtcraft:glass")), 0);
        QCOMPARE(m_client->chatLines.size(), 3);
        cleanup();

        QcServerConfig config;
        config.allowCheats = false;
        join(config);
        chat(u"/give glass 1"_s);
        QCOMPARE(countOf(item("qtcraft:glass")), 0);
        QVERIFY(m_client->chatLines.constLast().contains(u"disabled"_s));
    }

    void playersAreSavedOnDisconnectAndChunksByAutosave()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 1;
        join(config);
        QVERIFY(m_server->world().setBlock(m_feet.offset(1, 0, 0), id("qtcraft:glass")));
        ticks(20);
        QVERIFY(QFile::exists(regionFile(dir.path(), m_feet.offset(1, 0, 0))));
        // The autosave wrote the player too; remove it so only the save on disconnect can recreate it.
        QVERIFY(QFile::remove(dir.filePath(u"players/Raw.qcp"_s)));
        m_client->connection.close();
        pump();
        ticks(1);
        QVERIFY(QFile::exists(dir.filePath(u"players/Raw.qcp"_s)));
    }

    void deathSurvivesReconnecting()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        m_client->connection.close();
        pump();
        ticks(1);
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->connection.send(QcMsg::Hello{QcProtocol::version, u"Raw"_s, {}});
        pump();
        ticks(2);
        QVERIFY(m_client->dead);
        QCOMPARE(m_client->health, 0.0);
        m_client->connection.send(QcMsg::Respawn{});
        pump();
        ticks(1);
        QVERIFY(!m_client->dead);
        QCOMPARE(m_client->health, 20.0);
    }

    void attacksNeedReachAndKillGivesLoot()
    {
        join();
        // A pig in a 1 x 1 pit two blocks deep next to the player cannot run away.
        const QcBlockPos pit = m_feet.offset(2, -1, 0);
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
        for (const QcItemEntityManager::Item &loot : m_server->items().items()) {
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

    void inventoryGridCraftsAndTakesOnce()
    {
        join();
        chat(u"/give oak_log 2"_s);
        QCOMPARE(m_client->craftingSize, 2);
        // One log into the grid's top left cell: four planks are on offer.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 1});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 1);
        QCOMPARE(m_client->craftingResult, (QcItemStack{item("qtcraft:oak_planks"), 4, 0}));
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 4);
        QVERIFY(m_client->craftingResult.isEmpty());
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 4);
        // The third column and row of the grid only exist at a crafting table.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot + 2, 1});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot + 6, 1});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 1);
        // Closing returns what is left in the grid.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot + 3, 0});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 0);
        send(QcMsg::CloseCrafting{});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 1);
    }

    void hostileCraftingRequestsConserveItems()
    {
        join();
        chat(u"/give oak_log 7"_s);
        const auto total = [this] {
            int planks = countOf(item("qtcraft:oak_planks"));
            int logs = countOf(item("qtcraft:oak_log"));
            for (const QcItemStack &cell : std::as_const(m_client->craftingGrid)) {
                logs += cell.item == item("qtcraft:oak_log") ? cell.count : 0;
            }
            // Four planks per log: the sum in logs never changes.
            return logs * 4 + planks;
        };
        const int before = total();
        const QList<int> slotNumbers{-1, 0, 8, 35, 36, 38, 42, 44, 45, 1000, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
        const QList<int> counts{-1, 0, 1, 3, 64, 1000, std::numeric_limits<int>::min()};
        qsizetype round = 0;
        for (const int from : slotNumbers) {
            for (const int to : slotNumbers) {
                m_client->connection.send(QcMsg::MoveInventoryItem{from, to, counts[round++ % counts.size()]});
            }
        }
        for (const int count : counts) {
            m_client->connection.send(QcMsg::CraftRecipe{u"qtcraft:oak_planks_from_oak_log"_s, count});
            m_client->connection.send(QcMsg::TakeCraftingResult{});
        }
        pump();
        ticks(2);
        QCOMPARE(total(), before);
        // 3 x 3-only cells stay empty without a table.
        for (const int cell : {2, 5, 6, 7, 8}) {
            QVERIFY(m_client->craftingGrid.value(cell).isEmpty());
        }
        for (const QcItemStack &stack : std::as_const(m_client->stacks)) {
            QVERIFY(stack.isEmpty() || (stack.count >= 1 && stack.count <= 64));
        }

        // The dead neither craft nor eat: give them something to craft from and something to eat first.
        chat(u"/give oak_log 2"_s);
        chat(u"/give raw_porkchop 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:raw_porkchop"))});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 1});
        QVERIFY(!m_client->craftingResult.isEmpty());
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        const QList<QcItemStack> stacks = m_client->stacks;
        send(QcMsg::CraftRecipe{u"qtcraft:oak_planks_from_oak_log"_s, 1});
        send(QcMsg::TakeCraftingResult{});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(m_client->stacks, stacks);
    }

    void gridItemsSurviveDisconnectAndServerStop()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/give oak_log 5"_s);
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 0});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 0);
        // Disconnect with the grid full, then rejoin: the logs are back in the inventory.
        m_client->connection.close();
        pump();
        ticks(1);
        const auto rejoin = [this] {
            auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
            m_server->addClient(std::move(serverEnd));
            m_client = std::make_unique<RawClient>(std::move(clientEnd));
            m_client->connection.send(QcMsg::Hello{QcProtocol::version, u"Raw"_s, {}});
            pump();
            ticks(2);
        };
        rejoin();
        QCOMPARE(countOf(item("qtcraft:oak_log")), 5);
        QVERIFY(std::ranges::all_of(m_client->craftingGrid, &QcItemStack::isEmpty));

        // The same through a server stop.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 0});
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        rejoin();
        QCOMPARE(countOf(item("qtcraft:oak_log")), 5);
    }

    void recipeBookIsAllOrNothingAndNeedsATableForBigRecipes()
    {
        join();
        chat(u"/give oak_planks 3"_s);
        send(QcMsg::CraftRecipe{u"qtcraft:crafting_table"_s, 1});
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 3);
        QCOMPARE(countOf(item("qtcraft:crafting_table")), 0);
        chat(u"/give oak_planks 9"_s);
        send(QcMsg::CraftRecipe{u"qtcraft:crafting_table"_s, 5});
        QCOMPARE(countOf(item("qtcraft:crafting_table")), 3);
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 0);

        chat(u"/give oak_planks 3"_s);
        chat(u"/give stick 2"_s);
        send(QcMsg::CraftRecipe{u"qtcraft:wooden_pickaxe"_s, 1});
        QCOMPARE(countOf(item("qtcraft:wooden_pickaxe")), 0);
        QVERIFY(m_client->chatLines.constLast().contains(u"crafting table"_s));
        send(QcMsg::CraftRecipe{u"qtcraft:no_such_recipe"_s, 1});
        QVERIFY(m_client->chatLines.constLast().contains(u"Unknown recipe"_s));

        // Place a table next to the player and use it: the 3 x 3 grid opens and the pickaxe works.
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:crafting_table"))});
        const QcBlockPos table = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{table.offset(0, -1, 0), QcBlockFace::Up});
        QCOMPARE(*m_server->world().blockAt(table), id("qtcraft:crafting_table"));
        send(QcMsg::UseItem{table, QcBlockFace::Up});
        QCOMPARE(m_client->openedCrafting, 3);
        QCOMPARE(m_client->craftingSize, 3);
        send(QcMsg::CraftRecipe{u"qtcraft:wooden_pickaxe"_s, 1});
        QCOMPARE(countOf(item("qtcraft:wooden_pickaxe")), 1);
        // Breaking the table away closes the big grid on the next take.
        m_server->world().setBlock(table, qcAirBlockId);
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(m_client->craftingSize, 2);
    }

    void eatingHealsOnlyWhenHurt()
    {
        join();
        chat(u"/give raw_porkchop 2"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:raw_porkchop"))});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:raw_porkchop")), 2);
        fallDownShaft();
        QCOMPARE(m_client->health, 13.0);
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:raw_porkchop")), 1);
        QVERIFY(m_client->health > 13.0);
    }

    void deathDropsTheInventoryOnlyWithoutKeepInventory()
    {
        join();
        QVERIFY(m_server->keepInventory());
        chat(u"/give dirt 5"_s);
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        QCOMPARE(countOf(item("qtcraft:dirt")), 5);
        QVERIFY(m_server->items().items().isEmpty());
        send(QcMsg::Respawn{});

        chat(u"/gamerule keepInventory false"_s);
        QVERIFY(!m_server->keepInventory());
        QVERIFY(m_client->chatLines.last().contains(u"false"_s));
        chat(u"/give stone_pickaxe"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:stone_pickaxe"))});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:dirt")), QcServer::craftingGridSlot, 2});
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        QCOMPARE(countOf(item("qtcraft:stone_pickaxe")), 0);
        QVERIFY(std::ranges::all_of(m_client->craftingGrid, &QcItemStack::isEmpty));
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);
        QCOMPARE(droppedCount(item("qtcraft:stone_pickaxe")), 1);
        // Dead players pick nothing up.
        ticks(30);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);
        // Respawned at the same spot, the player gathers the items again; none get lost on the way.
        send(QcMsg::Respawn{});
        idle(40);
        QCOMPARE(countOf(item("qtcraft:dirt")) + droppedCount(item("qtcraft:dirt")), 5);
        QVERIFY(countOf(item("qtcraft:dirt")) > 0);
    }

    void gameRuleNeedsCheatsAndAValue()
    {
        QcServerConfig config;
        config.allowCheats = false;
        config.keepInventory = false;
        join(config);
        chat(u"/gamerule keepInventory"_s);
        QVERIFY(m_client->chatLines.last().contains(u"false"_s));
        chat(u"/gamerule keepInventory true"_s);
        QVERIFY(m_client->chatLines.last().contains(u"disabled"_s));
        QVERIFY(!m_server->keepInventory());
        cleanup();
        join();
        chat(u"/gamerule keepInventory maybe"_s);
        QVERIFY(m_client->chatLines.last().startsWith(u"Usage"_s));
        QVERIFY(m_server->keepInventory());
    }

    void droppedItemsAreThrownAndSentToClients()
    {
        join();
        chat(u"/give dirt 5"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:dirt"))});
        send(QcMsg::DropItem{false});
        QCOMPARE(countOf(item("qtcraft:dirt")), 4);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 1);
        QCOMPARE(m_server->items().items().first().pickupDelay > 0.0, true);
        send(QcMsg::DropItem{true});
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);
        // Nothing to drop: nothing happens.
        send(QcMsg::DropItem{true});
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);
        ticks(2);
        int seen = 0;
        for (const QcItemStack &stack : std::as_const(m_client->items)) {
            QCOMPARE(stack.item, item("qtcraft:dirt"));
            seen += stack.count;
        }
        QCOMPARE(seen, 5);
        // Thrown items land a few metres ahead, out of the player's reach.
        ticks(int(QcItemEntityManager::thrownPickupDelay * QcPhysics::tickRate) + 5);
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        for (const QcItemEntityManager::Item &thrown : m_server->items().items()) {
            QVERIFY((thrown.motion.position - m_client->motion.position).length() > 1.5);
        }
    }

    void itemsMergeAndClientsLearnTheNewCount()
    {
        join();
        const QcVec3 spot{m_feet.x + 2.5, double(m_feet.y), m_feet.z + 0.5};
        QcItemEntityManager::Drop drop;
        drop.stack = {item("qtcraft:dirt"), 3, 0};
        drop.position = spot;
        m_server->items().spawn(drop, 9001);
        drop.position = spot + QcVec3{0.3, 0.0, 0.0};
        m_server->items().spawn(drop, 9002);
        ticks(1);
        QCOMPARE(m_client->items.size(), 2);
        ticks(QcItemEntityManager::mergeIntervalTicks + 1);
        QCOMPARE(m_client->items.size(), 1);
        QCOMPARE(m_client->items.begin()->count, 6);
    }

    void saplingsArePlantedOnSuitableGroundAndBreakWithIt()
    {
        join();
        chat(u"/give oak_sapling 3"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:oak_sapling"))});
        const QcBlockPos grass = m_feet.offset(2, -1, 0);
        send(QcMsg::UseItem{grass, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(grass.offset(0, 1, 0)), std::optional<QcBlockId>(id("qtcraft:oak_sapling")));
        QCOMPARE(m_server->plants().saplings().size(), 1);
        QCOMPARE(countOf(item("qtcraft:oak_sapling")), 2);
        // Not on stone, and not on another sapling.
        const QcBlockPos stone = m_feet.offset(-2, -1, 0);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        send(QcMsg::UseItem{stone, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(stone.offset(0, 1, 0)), std::optional<QcBlockId>(qcAirBlockId));
        send(QcMsg::UseItem{grass.offset(0, 1, 0), QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(grass.offset(0, 2, 0)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(countOf(item("qtcraft:oak_sapling")), 2);

        // Breaking the ground breaks the sapling, which drops itself.
        const double grassSeconds = QcMining::breakSeconds(m_content->block(id("qtcraft:grass_block")), nullptr);
        send(QcMsg::StartMining{grass});
        ticks(int(std::ceil(grassSeconds * QcPhysics::tickRate)));
        send(QcMsg::FinishMining{grass});
        QCOMPARE(m_server->world().blockAt(grass), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(m_server->world().blockAt(grass.offset(0, 1, 0)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(droppedCount(item("qtcraft:oak_sapling")), 1);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 1);
        ticks(1);
        QVERIFY(m_server->plants().saplings().isEmpty());
    }

    void despawningSaplingsPlantThemselves()
    {
        join();
        // Twenty old sapling items on grass, one per block; with a chance of 3/4 each, some take root.
        int spawned = 0;
        for (int dx = -3; dx <= 3 && spawned < 20; ++dx) {
            for (int dz = -3; dz <= 3 && spawned < 20; ++dz) {
                if (std::abs(dx) + std::abs(dz) < 2) {
                    continue;
                }
                QcItemEntityManager::Drop drop;
                drop.stack = {item("qtcraft:oak_sapling"), 1, 0};
                drop.position = {m_feet.x + dx + 0.5, double(m_feet.y), m_feet.z + dz + 0.5};
                drop.age = QcItemEntityManager::despawnSeconds - 0.01;
                drop.pickupDelay = 10.0;
                m_server->items().spawn(drop, quint32(9100 + spawned++));
            }
        }
        ticks(1);
        QVERIFY(m_server->items().items().isEmpty());
        int planted = 0;
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                planted += m_server->world().blockAt(m_feet.offset(dx, 0, dz)) == id("qtcraft:oak_sapling") ? 1 : 0;
            }
        }
        QVERIFY2(planted >= 8 && planted <= 20, qPrintable(QString::number(planted)));
        QCOMPARE(m_server->plants().saplings().size(), planted);
        // Other items just vanish.
        QcItemEntityManager::Drop dirt;
        dirt.stack = {item("qtcraft:dirt"), 1, 0};
        dirt.position = {m_feet.x + 0.5, double(m_feet.y) + 3.0, m_feet.z + 3.5};
        dirt.age = QcItemEntityManager::despawnSeconds - 0.01;
        m_server->items().spawn(dirt, 9200);
        ticks(1);
        QVERIFY(m_server->items().items().isEmpty());
    }

    void crowdedCraftingGridSpillsOntoTheGround()
    {
        join();
        chat(u"/give oak_log 4"_s);
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 0});
        chat(u"/give stone 2304"_s);
        QCOMPARE(countOf(item("qtcraft:stone")), 36 * 64);
        send(QcMsg::CloseCrafting{});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 0);
        QCOMPARE(droppedCount(item("qtcraft:oak_log")), 4);
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
        QCOMPARE(m_server->plants().saplings().size(), 1);
        chat(u"/setblock %1 %2 %3 glass"_s.arg(m_feet.x + 1).arg(m_feet.y).arg(m_feet.z + 2));
        QCOMPARE(m_server->world().blockAt(m_feet.offset(1, 0, 2)), std::optional<QcBlockId>(id("qtcraft:glass")));
        // Air breaks the block below the sapling without drops; the sapling falls off and drops itself.
        chat(u"/setblock ~2 ~-1 ~1 air"_s);
        QCOMPARE(m_server->world().blockAt(m_feet.offset(2, 0, 1)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(droppedCount(item("qtcraft:oak_sapling")), 1);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 7);
        chat(u"/setblock ~ 9999 ~ glass"_s);
        QVERIFY(m_client->chatLines.last().startsWith(u"Usage"_s));
        chat(u"/setblock ~x ~ ~ glass"_s);
        QVERIFY(m_client->chatLines.last().startsWith(u"Usage"_s));
        cleanup();
        QcServerConfig noCheats;
        noCheats.allowCheats = false;
        join(noCheats);
        chat(u"/spawnitem dirt"_s);
        chat(u"/setblock ~1 ~ ~ glass"_s);
        QVERIFY(m_server->items().items().isEmpty());
        QCOMPARE(m_server->world().blockAt(m_feet.offset(1, 0, 0)), std::optional<QcBlockId>(qcAirBlockId));
    }

    void stoppingEmptiesOpenGridsWithoutRepairingTools_data()
    {
        QTest::addColumn<bool>("disconnectFirst");
        QTest::newRow("connected") << false;
        // The game closes the connection before it stops the server.
        QTest::newRow("disconnected") << true;
    }

    void stoppingEmptiesOpenGridsWithoutRepairingTools()
    {
        QFETCH(bool, disconnectFirst);
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/give stone_pickaxe"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:stone_pickaxe"))});
        // Wear the pickaxe once on the grass next to the player.
        const QcBlockPos grass = m_feet.offset(1, -1, 0);
        send(QcMsg::StartMining{grass});
        ticks(40);
        send(QcMsg::FinishMining{grass});
        QCOMPARE(m_server->world().blockAt(grass), std::optional<QcBlockId>(qcAirBlockId));
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:stone_pickaxe")), QcServer::craftingGridSlot, 0});
        QCOMPARE(m_client->craftingGrid.first().damage, 1);
        // A full inventory: the pickaxe cannot go back and must land on the ground, still worn.
        chat(u"/give stone 2304"_s);
        if (disconnectFirst) {
            m_client->connection.close();
            pump();
        }
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        int pickaxes = 0;
        for (const QcItemEntityManager::Item &dropped : m_server->items().items()) {
            if (dropped.stack.item == item("qtcraft:stone_pickaxe")) {
                ++pickaxes;
                QCOMPARE(dropped.stack.damage, 1);
            }
        }
        QCOMPARE(pickaxes, 1);
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
        bob.connection.send(QcMsg::Hello{QcProtocol::version, u"Bob"_s, look});
        pump();
        ticks(2);
        const quint32 bobId = std::ranges::find_if(m_client->spawns, [](const QcMsg::EntitySpawn &spawn) {
                                  return spawn.name == u"Bob"_s;
                              })->id;
        const QcMsg::EntitySpawn seen = m_client->spawns.value(bobId);
        QCOMPARE(seen.mobType, QcMsg::EntitySpawn::playerType);
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
        // Using an item swings Bob's arm for everyone else.
        bob.connection.send(QcMsg::UseItem{m_feet.offset(1, -1, 0), QcBlockFace::Up});
        pump();
        ticks(2);
        QVERIFY((m_client->flags.value(bobId) & QcMsg::Swing) != 0);
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
        bob.connection.send(QcMsg::Hello{QcProtocol::version, u"Bob"_s, {}});
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
        bob.connection.send(QcMsg::Hello{QcProtocol::version, u"Bob"_s, {}});
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

    void itemsSaplingsAndGameRulesSurviveARestart()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/gamerule keepInventory false"_s);
        QcItemEntityManager::Drop drop;
        drop.stack = {item("qtcraft:stone_pickaxe"), 1, 7};
        drop.position = {m_feet.x + 2.5, double(m_feet.y), m_feet.z + 2.5};
        m_server->items().spawn(drop, 9300);
        const QcBlockPos sapling = m_feet.offset(-2, 0, -2);
        QVERIFY(m_server->world().setBlock(sapling, id("qtcraft:birch_sapling")));
        m_server->plants().saplingPlaced(sapling, id("qtcraft:birch_sapling"));
        ticks(1);
        QVERIFY(m_server->stop().has_value());
        QVERIFY(QFile::exists(dir.filePath(u"entities.qce"_s)));
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        QVERIFY(!m_server->keepInventory());
        QCOMPARE(m_server->items().items().size(), 1);
        QCOMPARE(m_server->items().items().first().stack, (QcItemStack{item("qtcraft:stone_pickaxe"), 1, 7}));
        QCOMPARE(m_server->plants().saplings().size(), 1);
        QCOMPARE(m_server->plants().saplings().first().pos, sapling);
    }

    void failedSavesAreRetried()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        const QString regions = dir.filePath(u"regions"_s);
        QVERIFY(QDir().mkpath(regions));
        QVERIFY(QFile::setPermissions(regions, QFile::ReadOwner | QFile::ExeOwner));
        QVERIFY(m_server->world().setBlock(m_feet.offset(1, 0, 0), id("qtcraft:glass")));
        QVERIFY(!m_server->save().has_value());
        QVERIFY(QFile::setPermissions(regions, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(m_server->save().has_value());
        QVERIFY(QFile::exists(regionFile(dir.path(), m_feet.offset(1, 0, 0))));
    }
};

QTEST_GUILESS_MAIN(TestServerValidation)
#include "test_server_validation.moc"
