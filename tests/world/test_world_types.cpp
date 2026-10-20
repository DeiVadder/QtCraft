#include "common/content/loader/QcBaseContent.h"
#include "common/protocol/QcInProcessTransport.h"
#include "QcRawClient.h"
#include "QcTestUtil.h"
#include "server/core/QcServer.h"
#include "worldgen/generators/QcFlatGenerator.h"
#include "worldgen/generators/QcVoidGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcWorldLibrary.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

int nonAirBlocks(const QcChunk &chunk)
{
    int count = 0;
    for (int y = 0; y < QcWorld::worldHeight; ++y) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                count += chunk.block(x, y, z) != qcAirBlockId ? 1 : 0;
            }
        }
    }
    return count;
}

bool sameChunks(const QcChunk &a, const QcChunk &b)
{
    for (int y = 0; y < QcWorld::worldHeight; ++y) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                if (a.block(x, y, z) != b.block(x, y, z)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

class TestWorldTypes : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcBlockId id(const char *name) const { return *m_content->blockId(QString::fromLatin1(name)); }

    // Joins a server of `type` and lets it run; returns where the player stands.
    QcVec3 joinAndSettle(const QString &type, const QString &options = {})
    {
        QcServerConfig config;
        config.generator = type;
        config.generatorOptions = options;
        config.viewDistance = 1;
        QcServer server(config, m_content, QcWorldGenerators::create(type, m_content, 0, options));
        if (!server.openWorld()) {
            return {};
        }
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        server.addClient(std::move(serverEnd));
        QcRawClient client(std::move(clientEnd));
        client.hello(u"Tester"_s);
        QcTestUtil::pump();
        for (int i = 0; i < 40; ++i) {
            client.connection.send(QcMsg::PlayerInput{QcPlayerInput{.sequence = quint32(i + 1)}});
            server.tick();
            QcTestUtil::pump();
        }
        const QcVec3 standing = client.motion.position;
        [[maybe_unused]] const QcStatus stopped = server.stop();
        return standing;
    }

private slots:
    void flatLayersFromAPreset()
    {
        // The base game's default: bedrock, 59 stone, 3 dirt, grass on top (y 63).
        const QcResult<QList<QcBlockId>> standard = QcFlatGenerator::parseLayers(m_content->worldgen().flatWorld().preset, *m_content);
        QVERIFY(standard);
        QCOMPARE(standard->size(), 64);
        QCOMPARE(standard->at(0), id("qtcraft:bedrock"));
        QCOMPARE(standard->at(1), id("qtcraft:stone"));
        QCOMPARE(standard->at(59), id("qtcraft:stone"));
        QCOMPARE(standard->at(60), id("qtcraft:dirt"));
        QCOMPARE(standard->at(63), id("qtcraft:grass_block"));

        const QcResult<QList<QcBlockId>> custom = QcFlatGenerator::parseLayers(u" bedrock , 2*qtcraft:sand,glass"_s, *m_content);
        QVERIFY(custom);
        QCOMPARE(*custom, (QList<QcBlockId>{id("qtcraft:bedrock"), id("qtcraft:sand"), id("qtcraft:sand"), id("qtcraft:glass")}));
    }

    void badLayersAreRefused_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::newRow("empty") << QString();
        QTest::newRow("unknown block") << u"bedrock,nothing"_s;
        QTest::newRow("zero") << u"0*stone"_s;
        QTest::newRow("not a number") << u"x*stone"_s;
        QTest::newRow("too high") << u"300*stone"_s;
    }

    void badLayersAreRefused()
    {
        QFETCH(QString, preset);
        QVERIFY(!QcFlatGenerator::parseLayers(preset, *m_content));
        if (!preset.isEmpty()) {
            QVERIFY(!QcWorldGenerators::checkOptions(u"flat"_s, preset, *m_content));
            // A world that has them anyway (edited world.json) falls back to the default.
            const QcFlatGenerator generator(m_content, preset);
            QCOMPARE(generator.warnings().size(), 1);
            QCOMPARE(generator.layers().size(), 64);
        }
    }

    void flatWorldIsTheSameEverywhere()
    {
        const auto generator = QcWorldGenerators::create(u"flat"_s, m_content, 1, u"bedrock,3*dirt,grass_block"_s);
        const auto again = QcWorldGenerators::create(u"flat"_s, m_content, 99, u"bedrock,3*dirt,grass_block"_s);
        const std::shared_ptr<QcChunk> origin = generator->generate({0, 0});
        QCOMPARE(nonAirBlocks(*origin), 5 * QcWorld::chunkSize * QcWorld::chunkSize);
        QCOMPARE(origin->highestNonAir(7, 7), 4);
        for (const QcChunkPos pos : {QcChunkPos{0, 0}, QcChunkPos{-30, 17}, QcChunkPos{1000, -1000}}) {
            QVERIFY(sameChunks(*generator->generate(pos), *origin));
            QVERIFY(sameChunks(*again->generate(pos), *origin));
        }
        QCOMPARE(generator->spawnPoint(), (QcBlockPos{0, 5, 0}));
        QVERIFY(generator->isNaturalGround(id("qtcraft:grass_block")));
        QVERIFY(!generator->isNaturalGround(id("qtcraft:dirt")));

        // Its rules: the day runs, no creatures of their own accord.
        const QcWorldSetup setup = generator->setup();
        QVERIFY(!setup.timeOfDay);
        QVERIFY(setup.daylightCycle);
        QVERIFY(!setup.mobSpawning);
        QVERIFY(setup.mobs.isEmpty());
    }

    void voidWorldIsEmptyButThePlatform()
    {
        const auto generator = QcWorldGenerators::create(u"void"_s, m_content, 5);
        // A 5 x 5 platform of stone at y 63 around the origin, split over four chunks.
        int platform = 0;
        for (const QcChunkPos pos : {QcChunkPos{0, 0}, QcChunkPos{-1, 0}, QcChunkPos{0, -1}, QcChunkPos{-1, -1}}) {
            const std::shared_ptr<QcChunk> chunk = generator->generate(pos);
            platform += nonAirBlocks(*chunk);
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    const int wx = pos.x * 16 + x;
                    const int wz = pos.z * 16 + z;
                    const bool onPlatform = std::abs(wx) <= 2 && std::abs(wz) <= 2;
                    QCOMPARE(chunk->highestNonAir(x, z), onPlatform ? 63 : -1);
                }
            }
        }
        QCOMPARE(platform, 25);
        QCOMPARE(nonAirBlocks(*generator->generate({3, -8})), 0);
        QCOMPARE(generator->spawnPoint(), (QcBlockPos{0, 64, 0}));
        QVERIFY(generator->isNaturalGround(id("qtcraft:stone")));
        QVERIFY(!generator->isNaturalGround(qcAirBlockId));

        const QcWorldSetup setup = generator->setup();
        QCOMPARE(setup.timeOfDay, std::optional(150.0));
        QVERIFY(!setup.daylightCycle);
        QVERIFY(!setup.mobSpawning);
        QVERIFY(!QcWorldGenerators::checkOptions(u"void"_s, u"stone"_s, *m_content));
        QVERIFY(QcWorldGenerators::checkOptions(u"void"_s, {}, *m_content));
    }

    void playersStandSafely()
    {
        // On the platform of the void world and on top of the flat layers; nobody falls.
        const QcVec3 onPlatform = joinAndSettle(u"void"_s);
        QCOMPARE(onPlatform.y, 64.0);
        QVERIFY(std::abs(onPlatform.x) < 3.0 && std::abs(onPlatform.z) < 3.0);
        QCOMPARE(joinAndSettle(u"flat"_s).y, 64.0);
        QCOMPARE(joinAndSettle(u"flat"_s, u"bedrock,3*dirt,grass_block"_s).y, 5.0);
    }

    void voidPlatformOptions()
    {
        const QcResult<QcVoidDefinition> custom = QcVoidGenerator::parseOptions(u" platform = glass , radius=0,y=10"_s, *m_content);
        QVERIFY2(custom, qPrintable(custom ? QString() : custom.error()));
        QCOMPARE(custom->platform, u"qtcraft:glass"_s);
        QCOMPARE(custom->platformRadius, 0);
        QCOMPARE(custom->platformY, 10);
        // Keys left out keep the content's settings.
        QCOMPARE(QcVoidGenerator::parseOptions(u"radius=4"_s, *m_content)->platform, u"qtcraft:stone"_s);

        for (const QString &bad : {u"stone"_s, u"platform=nothing"_s, u"radius=17"_s, u"y=0"_s, u"height=3"_s, u"radius=x"_s}) {
            QVERIFY2(!QcWorldGenerators::checkOptions(u"void"_s, bad, *m_content), qPrintable(bad));
        }
        const auto platform = QcWorldGenerators::create(u"void"_s, m_content, 0, u"platform=glass,radius=1,y=20"_s);
        QCOMPARE(nonAirBlocks(*platform->generate({0, 0})), 4);
        QCOMPARE(platform->generate({0, 0})->block(1, 20, 1), id("qtcraft:glass"));
        QCOMPARE(platform->spawnPoint(), (QcBlockPos{0, 21, 0}));
    }

    void optionsAreLimitedAsGiven()
    {
        // Empty entries and blanks count: such options could not be stored and read back.
        const QString padded = QString(QcWorldGenerators::maxOptionsLength, u',') + u"stone"_s;
        QVERIFY(QcFlatGenerator::parseLayers(padded, *m_content));
        QVERIFY(!QcWorldGenerators::checkOptions(u"flat"_s, padded, *m_content));
        QVERIFY(!QcWorldGenerators::optionsToStore(u"flat"_s, padded, *m_content));
        const QString blanks = u"stone"_s + QString(QcWorldGenerators::maxOptionsLength, u' ');
        QVERIFY(!QcWorldGenerators::checkOptions(u"flat"_s, blanks, *m_content));

        // Up to the limit they are fine.
        const QString longest = QString(QcWorldGenerators::maxOptionsLength - 5, u',') + u"stone"_s;
        QVERIFY(QcWorldGenerators::checkOptions(u"flat"_s, longest, *m_content));
    }

    void worldsKeepTheDefaultsTheyWereMadeWith()
    {
        // Worlds made without options store the defaults of their type as they are now.
        const QcResult<QString> flatOptions = QcWorldGenerators::optionsToStore(u"flat"_s, {}, *m_content);
        const QcResult<QString> voidOptions = QcWorldGenerators::optionsToStore(u"void"_s, {}, *m_content);
        QVERIFY(flatOptions && voidOptions);
        QCOMPARE(*flatOptions, m_content->worldgen().flatWorld().preset);
        QCOMPARE(*voidOptions, u"platform=qtcraft:stone,radius=2,y=63"_s);
        QCOMPARE(*QcWorldGenerators::optionsToStore(u"flat"_s, u"bedrock,sand"_s, *m_content), u"bedrock,sand"_s);
        QCOMPARE(*QcWorldGenerators::optionsToStore(u"terrain"_s, {}, *m_content), QString());

        QTemporaryDir directory;
        const QcWorldLibrary library(directory.path());
        QVERIFY(library.create(u"Flat"_s, 1, {.generator = u"flat"_s, .options = *flatOptions, .gameMode = QcGameMode::Survival}));
        QVERIFY(library.create(u"Void"_s, 1, {.generator = u"void"_s, .options = *voidOptions, .gameMode = QcGameMode::Survival}));

        // Then an update or a mod changes those defaults.
        const std::shared_ptr<QcContentRegistry> changed = QcTestUtil::baseContent()->registry;
        changed->worldgen().setFlatWorld({u"bedrock,3*sand"_s, changed->worldgen().flatWorld().rules});
        changed->worldgen().setVoidWorld({u"qtcraft:glass"_s, 0, 10, changed->worldgen().voidWorld().rules});

        // Reopened from what the library lists, both look as they did.
        const QList<QcWorldLibrary::Entry> entries = library.list();
        QCOMPARE(entries.size(), 2);
        for (const QcWorldLibrary::Entry &entry : entries) {
            QcServerConfig config;
            config.worldDirectory = entry.directory;
            config.seed = entry.info.seed;
            config.generator = entry.info.generator;
            config.generatorOptions = entry.info.generatorOptions;
            std::shared_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(config.generator, changed, config.seed, config.generatorOptions);
            const std::shared_ptr<QcChunk> origin = generator->generate({0, 0});
            QCOMPARE(origin->highestNonAir(0, 0), 63);
            QCOMPARE(origin->block(0, 63, 0), id(entry.info.generator == u"flat"_s ? "qtcraft:grass_block" : "qtcraft:stone"));

            QcServer server(config, changed, QcWorldGenerators::create(config.generator, changed, config.seed, config.generatorOptions));
            QVERIFY(server.openWorld());
            QVERIFY(server.stop());
        }
    }

    void typeAndLayersAreStoredWithTheWorld()
    {
        QTemporaryDir directory;
        const QcWorldLibrary library(directory.path());
        const QcResult<QcWorldLibrary::Entry> entry = library.create(u"Flat"_s, 3, {.generator = u"flat"_s, .options = u"bedrock,grass_block"_s, .gameMode = QcGameMode::Survival});
        QVERIFY(entry);
        const QcWorldLibrary::Entry listed = library.list().constFirst();
        QCOMPARE(listed.info.generator, u"flat"_s);
        QCOMPARE(listed.info.generatorOptions, u"bedrock,grass_block"_s);

        QcServerConfig config;
        config.worldDirectory = entry->directory;
        config.seed = 3;
        config.generator = u"flat"_s;
        config.generatorOptions = u"bedrock,grass_block"_s;
        {
            QcServer server(config, m_content, QcWorldGenerators::create(u"flat"_s, m_content, 3, config.generatorOptions));
            QVERIFY(server.openWorld());
            server.tick();
            QVERIFY(!server.mobs().naturalSpawning());
            QVERIFY(server.stop());
        }

        // Other layers are another world.
        config.generatorOptions = u"bedrock,sand"_s;
        QcServer other(config, m_content, QcWorldGenerators::create(u"flat"_s, m_content, 3, config.generatorOptions));
        QVERIFY(!other.openWorld());
    }
};

QTEST_GUILESS_MAIN(TestWorldTypes)
#include "test_world_types.moc"
