#pragma once

#include "common/content/loader/QcBaseContent.h"
#include "common/protocol/QcInProcessTransport.h"
#include "QcRawClient.h"
#include "server/core/QcServer.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QTest>

#include <algorithm>
#include <cmath>

// A server with a flat stage around the spawn and one raw protocol client, the base of the server
// test classes.
class QcServerTestFixture : public QObject
{
    Q_OBJECT

protected:
    using RawClient = QcRawClient;

    static std::shared_ptr<const QcContentRegistry> loadContent()
    {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }

    static void pump()
    {
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
        }
    }

    // Which other chunks of the stage around the origin count as modified depends on generation
    // timing, so tests only look for the region of the block they changed.
    static QString regionFile(const QString &world, QcBlockPos pos)
    {
        const QcChunkPos region = QcWorldStorage::regionOf(pos.chunk());
        return QDir(world).filePath(QStringLiteral("regions/r.%1.%2.qcr").arg(region.x).arg(region.z));
    }

    std::shared_ptr<const QcContentRegistry> m_content = loadContent();
    std::unique_ptr<QcServer> m_server;
    std::unique_ptr<RawClient> m_client;
    QcBlockPos m_feet;
    quint32 m_sequence = 1000;
    // How the raw client of the next join() connects: like a client over the network, or like the
    // local player of the in-process host.
    QcServer::ClientOrigin m_origin = QcServer::ClientOrigin::Remote;
    // Chunks streamed around the raw client's player.
    int m_viewDistance = 1;
    // Changed chunks leave the world like in the game (tests of unloading, stage 31.3).
    bool m_unloadChunks = false;
    // Only changed chunks stay, as before stage 31.3 (comparison runs).
    bool m_keepChangedChunks = false;
    // Chunks load at once instead of in the background (comparison runs).
    bool m_loadChunksAtOnce = false;

    // A block of the content; a name the content does not have stops the test.
    QcBlockId id(const char *name) const
    {
        const std::optional<QcBlockId> found = m_content->blockId(QString::fromLatin1(name));
        if (!found) {
            qFatal("Test uses the block %s, which the content does not have", name);
        }
        return *found;
    }

    // Makes sure the block at `pos` has its container or machine (decision E-357); blocks set
    // through the world have them already, unless the table was full.
    bool ensureEntity(QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = m_server->world().blockAt(pos);
        return block && m_server->blockEntities().ensure(pos, *block);
    }

    // Every container and every machine of the world.
    QList<QcBlockEntities::Container> allContainers() const
    {
        QList<QcBlockEntities::Container> all;
        const QList<QcChunkPos> chunks = m_server->blockEntities().chunks();
        for (const QcChunkPos &chunk : chunks) {
            all.append(m_server->blockEntities().chunk(chunk)->containers.values());
        }
        return all;
    }

    QList<QcBlockEntities::Machine> allMachines() const
    {
        QList<QcBlockEntities::Machine> all;
        const QList<QcChunkPos> chunks = m_server->blockEntities().chunks();
        for (const QcChunkPos &chunk : chunks) {
            all.append(m_server->blockEntities().chunk(chunk)->machines.values());
        }
        return all;
    }

    // What circuit components remember at `pos`.
    bool burntOut(QcBlockPos pos) const
    {
        const QcBlockEntities::Memory *memory = m_server->blockEntities().memory(pos);
        return memory && memory->burntOutUntil != 0;
    }

    bool poweredOpener(QcBlockPos pos) const
    {
        const QcBlockEntities::Memory *memory = m_server->blockEntities().memory(pos);
        return memory && memory->powered;
    }
    // An item of the content; a name the content does not have stops the test (it would be garbage).
    QcItemId item(const char *name) const
    {
        const std::optional<QcItemId> found = m_content->itemId(QString::fromLatin1(name));
        if (!found) {
            qFatal("Test uses the item %s, which the content does not have", name);
        }
        return *found;
    }

    // Loads the chunks within `radius` of the chunk of `centre` now: what was kept of them comes back
    // with them (stored chunks are read when needed, stage 31.3).
    void loadAround(QcBlockPos centre, int radius = 1)
    {
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                m_server->world().loadChunk({centre.chunk().x + dx, centre.chunk().z + dz});
            }
        }
    }

    // Loads every chunk the world's storage holds, as a world was opened before stage 31.3 (comparison
    // runs), in a fixed order.
    void loadStored()
    {
        QList<QcChunkPos> stored = m_server->world().storedChunks();
        std::ranges::sort(stored, {}, [](QcChunkPos chunk) { return std::pair{chunk.x, chunk.z}; });
        for (const QcChunkPos &chunk : std::as_const(stored)) {
            m_server->world().loadChunk(chunk);
        }
    }

    void ticks(int count)
    {
        for (int i = 0; i < count; ++i) {
            m_server->tick();
            pump();
        }
    }

    // Joins a server on the test terrain, or on the world of `generator` if given. The raw client
    // is an operator unless the config names other operators (notOperator()).
    void join(QcServerConfig config = {}, std::unique_ptr<QcWorldGenerator> generator = nullptr)
    {
        config.viewDistance = m_viewDistance;
        // Most tests build away from their player; those of unloading set m_unloadChunks.
        config.keepChunksLoaded = config.keepChunksLoaded || (!m_unloadChunks && !m_keepChangedChunks);
        config.keepChangedChunks = config.keepChangedChunks || m_keepChangedChunks;
        config.loadChunksAtOnce = config.loadChunksAtOnce || m_loadChunksAtOnce;
        if (config.operators.isEmpty()) {
            config.operators = {QStringLiteral("Raw")};
        }
        if (!generator) {
            generator = std::make_unique<QcTestTerrainGenerator>(m_content);
        }
        m_server = std::make_unique<QcServer>(config, m_content, std::move(generator));
        QVERIFY(m_server->openWorld().has_value());

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(QStringLiteral("Raw"));
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

    // A config whose raw client is no operator.
    static QcServerConfig notOperator(QcServerConfig config = {})
    {
        config.operators = {QStringLiteral("SomebodyElse")};
        return config;
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
        for (const QcItemEntity &dropped : m_server->items().items()) {
            total += dropped.stack.item == wanted ? dropped.stack.count : 0;
        }
        return total;
    }

    // Places a furnace two blocks east of the player, standing on the stage.
    QcBlockPos placeFurnace()
    {
        chat(QStringLiteral("/give furnace"));
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:furnace"))});
        const QcBlockPos pos = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{pos.offset(0, -1, 0), QcBlockFace::Up});
        return pos;
    }

    QcBlockPos openFurnace()
    {
        const QcBlockPos pos = placeFurnace();
        send(QcMsg::UseItem{pos, QcBlockFace::West});
        return pos;
    }

    void move(int from, int to, int count = 0) { send(QcMsg::MoveInventoryItem{from, to, count}); }

    static constexpr int input = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int fuel = QcMsg::MoveInventoryItem::machineFuelSlot;
    static constexpr int output = QcMsg::MoveInventoryItem::machineOutputSlot;
};
