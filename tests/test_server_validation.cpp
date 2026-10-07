#include "QcBaseContent.h"
#include "QcConnection.h"
#include "QcInProcessTransport.h"
#include "QcMining.h"
#include "QcServer.h"
#include "QcTestTerrainGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
        } else if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
            chatLines.append(chat->text);
        }
    }

    QcConnection connection;
    QcEntityMotion motion;
    QList<QcItemStack> stacks;
    QStringList chatLines;
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
        QCOMPARE(QDir(dir.filePath(u"regions"_s)).entryList({u"*.qcr"_s}).size(), 1);
        m_client->connection.close();
        pump();
        ticks(1);
        QVERIFY(QFile::exists(dir.filePath(u"players/Raw.qcp"_s)));
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
        QCOMPARE(QDir(regions).entryList({u"*.qcr"_s}).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestServerValidation)
#include "test_server_validation.moc"
