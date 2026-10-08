#include "QcBaseContent.h"
#include "QcConnection.h"
#include "QcInProcessTransport.h"
#include "QcMining.h"
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
        m_client->connection.send(QcMsg::Hello{QcProtocol::version, u"Raw"_s});
        pump();
        ticks(2);
        const QcVec3 p = m_client->motion.position;
        m_feet = {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
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
        QCOMPARE(countOf(item("qtcraft:dirt")), 1);
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
        m_client->connection.send(QcMsg::Hello{QcProtocol::version, u"Raw"_s});
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
        QCOMPARE(countOf(item("qtcraft:raw_porkchop")), 2);

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
            m_client->connection.send(QcMsg::Hello{QcProtocol::version, u"Raw"_s});
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
