#pragma once

#include "QcBaseContent.h"
#include "QcInProcessTransport.h"
#include "QcRawClient.h"
#include "QcServer.h"
#include "QcTestTerrainGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QTest>

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
        m_client->connection.send(QcMsg::Hello{QcProtocol::version, QStringLiteral("Raw"), {}});
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
