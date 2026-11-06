// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcTestUtil.h"
#include "fixtures/QcTestWorld.h"
#include "server/blocks/QcLiquidFlow.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerState.h"
#include "server/dimensions/QcDimensions.h"
#include "server/versions/QcGeneratorSwitch.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/nether/QcNetherGenerator.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

// The nether in a world: its own series of generator versions (decision E-1033), worlds made with
// the placeholder before it, its springs on the server (E-1031) and the showcase's station (E-1034).
namespace {

const QString nether = u"qtcraft:nether"_s;
constexpr int seed = 4711;

QcServerConfig configIn(const QString &directory)
{
    QcServerConfig config;
    config.worldName = u"Nether"_s;
    config.seed = seed;
    config.viewDistance = 2;
    config.worldDirectory = directory;
    config.keepChunksLoaded = true;
    return config;
}

QJsonObject readWorld(const QString &directory)
{
    QFile file(QDir(directory).filePath(u"world.json"_s));
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

void writeWorld(const QString &directory, const QJsonObject &json)
{
    QFile file(QDir(directory).filePath(u"world.json"_s));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(json).toJson()) < 0) {
        qFatal("cannot write world.json");
    }
}

// The entry of the nether in world.json at `directory`.
QJsonObject netherEntry(const QString &directory)
{
    return readWorld(directory).value(u"generator"_s).toObject().value(u"dimensions"_s).toObject().value(nether).toObject();
}

// Sets the entry of the nether in world.json at `directory`; an empty one takes all entries out
// (a world of release 1.0.0).
void setNetherEntry(const QString &directory, const QJsonObject &entry)
{
    QJsonObject json = readWorld(directory);
    QJsonObject generator = json.value(u"generator"_s).toObject();
    if (entry.isEmpty()) {
        generator.remove(u"dimensions"_s);
    } else {
        QJsonObject dimensions = generator.value(u"dimensions"_s).toObject();
        dimensions.insert(nether, entry);
        generator.insert(u"dimensions"_s, dimensions);
    }
    json.insert(u"generator"_s, generator);
    writeWorld(directory, json);
}

} // namespace

class TestNetherDimension : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // A saved world in `directory`.
    void makeWorld(const QString &directory) const
    {
        QcServer server(configIn(directory), m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(server.openWorld().has_value());
        QVERIFY(server.save().has_value());
    }

private slots:
    // A new world counts the nether in its own series from the start.
    void aNewWorldKeepsTheSeriesBesideTheNumber()
    {
        QTemporaryDir dir;
        makeWorld(dir.path());
        const QJsonObject entry = netherEntry(dir.path());
        QCOMPARE(entry.value(u"version"_s).toInt(), QcGeneratorVersion::nether.current);
        QCOMPARE(entry.value(u"series"_s).toString(), u"nether"_s);

        QcServer server(configIn(dir.path()), m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(server.openWorld().has_value());
        const QcResult<QcServerDimension *> opened = server.openDimension(nether);
        QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error()));
        QCOMPARE((*opened)->generatorVersion(), QcGeneratorVersion::nether.current);
        QVERIFY(dynamic_cast<const QcNetherGenerator *>(&dynamic_cast<const QcVersionedGenerator &>((*opened)->world().generator()).base()));
    }

    // A world of the release before the nether gets it as a late dimension, and keeps the entry.
    void aWorldOfTheReleaseGetsTheNetherLate()
    {
        QTemporaryDir dir;
        makeWorld(dir.path());
        setNetherEntry(dir.path(), {});
        {
            QcServer server(configIn(dir.path()), m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            QVERIFY(server.openWorld().has_value());
            const QcResult<QcServerDimension *> opened = server.openDimension(nether);
            QVERIFY(opened.has_value());
            QCOMPARE((*opened)->generatorVersion(), QcGeneratorVersion::nether.forLateDimension());
            QVERIFY(server.save().has_value());
        }
        QCOMPARE(netherEntry(dir.path()).value(u"series"_s).toString(), u"nether"_s);
        QCOMPARE(netherEntry(dir.path()).value(u"version"_s).toInt(), QcGeneratorVersion::nether.forLateDimension());
    }

    // The placeholder's number is one of the overworld's (decision E-1026): the same digit in the
    // nether's series would be another world, so the nether stays shut with the reason, the rest
    // of the world plays on, and the entry stays as it was.
    void aPlaceholderWorldIsRefused_data()
    {
        QTest::addColumn<int>("version");
        QTest::addRow("the overworld's current") << QcGeneratorVersion::current;
        QTest::addRow("a number the nether has too") << 1;
    }
    void aPlaceholderWorldIsRefused()
    {
        QFETCH(int, version);
        QTemporaryDir dir;
        makeWorld(dir.path());
        const QJsonObject placeholder{{u"version"_s, version}};
        setNetherEntry(dir.path(), placeholder);

        {
            QcServer server(configIn(dir.path()), m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            QVERIFY(server.openWorld().has_value());
            const QcResult<QcServerDimension *> opened = server.openDimension(nether);
            QVERIFY(!opened.has_value());
            QVERIFY2(opened.error().contains(u"overworld generators"_s), qPrintable(opened.error()));
            QVERIFY(server.save().has_value());
        }
        QCOMPARE(netherEntry(dir.path()), placeholder);

        QcWorldInfo info;
        info.dimensionGeneratorVersions.insert(nether, version);
        QCOMPARE(QcDimensions::versionWarnings(*m_content, info).size(), 1);
        const QcResult<QcGeneratorSwitch::Switched> switched = QcGeneratorSwitch::switchDimension(dir.path(), nether, 1, m_content);
        QVERIFY(!switched.has_value());
    }

    // A number of the nether's own series this game does not generate is refused as ever.
    void aNewerNetherIsRefused()
    {
        QTemporaryDir dir;
        makeWorld(dir.path());
        setNetherEntry(dir.path(), {{u"version"_s, QcGeneratorVersion::nether.current + 1}, {u"series"_s, u"nether"_s}});
        QcServer server(configIn(dir.path()), m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(server.openWorld().has_value());
        const QcResult<QcServerDimension *> opened = server.openDimension(nether);
        QVERIFY(!opened.has_value());
        QVERIFY2(opened.error().contains(u"this game generates versions"_s), qPrintable(opened.error()));
    }

    // A spring runs once its chunk has arrived: the cell in front of it fills with lava.
    void springsRunWhenTheirChunkArrives()
    {
        QTemporaryDir dir;
        QcServer server(configIn(dir.path()), m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(server.openWorld().has_value());
        const QcResult<QcServerDimension *> opened = server.openDimension(nether);
        QVERIFY(opened.has_value());
        QcServerWorld &world = (*opened)->world();

        const QcNetherGenerator generator(m_content, seed);
        std::optional<QcBlockPos> spring;
        for (int z = -8; z < 8 && !spring; ++z) {
            for (int x = -8; x < 8 && !spring; ++x) {
                const QList<QcBlockPos> cells = generator.liquidStartsIn(*generator.generate({x, z}));
                spring = cells.isEmpty() ? std::nullopt : std::optional(cells.first());
            }
        }
        QVERIFY(spring);

        world.loadChunk(spring->chunk());
        const QcBlockId lava = *m_content->blockId(u"qtcraft:lava"_s);
        const auto flowed = [&] {
            return std::ranges::any_of(std::array{QcBlockPos{1, 0, 0}, QcBlockPos{-1, 0, 0}, QcBlockPos{0, 0, 1}, QcBlockPos{0, 0, -1}}, [&](QcBlockPos side) {
                const std::optional<QcBlockId> block = world.blockAt(spring->offset(side.x, 0, side.z));
                return block && m_content->isSameBlock(*block, lava);
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT((QcTestUtil::ticks(server, 5), world.blockAt(*spring) == std::optional(lava)), 10000);
        QTRY_VERIFY_WITH_TIMEOUT((QcTestUtil::ticks(server, 20), flowed()), 20000);
    }

    // The role model's lava: in the nether a source runs seven blocks and a step takes 10 ticks, in
    // the overworld three and 30 (E-1035).
    void lavaFlowsFurtherAndFasterInTheNether()
    {
        const QcBlockId lava = *m_content->blockId(u"qtcraft:lava"_s);
        const QcBlockId stone = *m_content->blockId(u"qtcraft:stone"_s);
        const auto reachAfter = [&](QcLiquidFlow &flow, quint64 ticks) {
            QcTestWorld world(*m_content);
            world.fillBox({-12, 0, -12}, {12, 0, 12}, stone);
            world.set({0, 1, 0}, lava);
            flow.schedule(world, {0, 1, 0}, 0);
            for (quint64 tick = 1; tick <= ticks; ++tick) {
                flow.tick(tick, world, [&world](QcBlockPos pos, QcBlockId block) { world.set(pos, block); });
            }
            flow.clear();
            int reach = 0;
            while (reach < 12 && *world.blockAt({reach + 1, 1, 0}) != qcAirBlockId) {
                ++reach;
            }
            return reach;
        };

        const QcDimensionDefinition *definition = m_content->worldgen().dimension(nether);
        QVERIFY(definition);
        QcLiquidFlow overworld(m_content);
        QcLiquidFlow inNether(m_content, definition->liquidFlows);
        QCOMPARE(reachAfter(overworld, 400), 3);
        QCOMPARE(reachAfter(inNether, 400), 7);
        QCOMPARE(reachAfter(overworld, 60), 2);
        QCOMPARE(reachAfter(inNether, 60), 6);
    }

    // The nether of a showcase has the station where its portal leads to; a versioned one has not.
    void theShowcaseHasAnArrivalStation()
    {
        const auto station = QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::nether), m_content, 1, QString::fromLatin1(QcWorldGenerators::netherStation), 1);
        QVERIFY(station);
        const QcShowcaseGenerator showcase(m_content);
        const auto portal = std::ranges::find_if(showcase.plan().placements, [](const QcStructurePlacement &placement) { return placement.structure().name() == u"qtcraft:portal_station"_s; });
        QVERIFY(portal != showcase.plan().placements.end());

        const QcBlockPos arrival = station->spawnPoint();
        QVERIFY(std::abs(arrival.x - portal->origin().x / 8) <= 1);
        QVERIFY(std::abs(arrival.z - portal->origin().z / 8) <= 1);
        const std::shared_ptr<QcChunk> chunk = station->generate(arrival.chunk());
        QCOMPARE(chunk->block(arrival.localX(), arrival.y, arrival.localZ()), qcAirBlockId);
        QVERIFY(chunk->block(arrival.localX(), arrival.y - 1, arrival.localZ()) != qcAirBlockId);

        // A station spans chunks; at least one of them carries labels.
        bool labelled = false;
        for (int z = -2; z <= 2 && !labelled; ++z) {
            for (int x = -2; x <= 2 && !labelled; ++x) {
                labelled = !station->labelsIn({arrival.chunk().x + x, arrival.chunk().z + z}).isEmpty();
            }
        }
        QVERIFY(labelled);
    }
};

QTEST_GUILESS_MAIN(TestNetherDimension)
#include "test_nether_dimension.moc"
