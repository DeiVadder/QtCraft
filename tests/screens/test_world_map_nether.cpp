// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldLibraryController.h"
#include "app/controllers/QcWorldOptionsController.h"
#include "app/preview/QcWorldMapPreview.h"
#include "app/preview/QcWorldMapTexts.h"
#include "app/preview/QcWorldMapWork.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcDimensionParser.h"
#include "common/content/worldgen/QcStructureParser.h"
#include "common/core/QcDowncast.h"
#include "common/core/QcIntMath.h"
#include "fixtures/QcTestUtil.h"
#include "server/dimensions/QcDimensions.h"
#include "worldgen/generators/QcDimensionMaps.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"
#include "worldgen/nether/QcNetherGenerator.h"
#include "worldgen/nether/QcNetherMap.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worlds/QcWorldLibrary.h"

#include <QColor>
#include <QJsonArray>
#include <QSet>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace Qt::StringLiterals;

namespace {

const QString overworldName = QString::fromLatin1(QcDimensionDefinition::overworldName);
const QString netherName = u"qtcraft:nether"_s;
const QString fortressKind = u"qtcraft:fortress"_s;
const QString evenRuin = u"test:even_ruin"_s;

} // namespace

// The dimensions on the map preview of a new world (decisions E-1077 to E-1079): the map shows the
// nether biomes of exactly the nether the server opens for the world, its sites are what the
// content places there, the switch lists the content's dimensions with a map, a mod's too, the
// choice stays when the seed changes, the view moves by the coordinate scale, and the spawn marker
// stands at the overworld generator's spawn or where the portals of the overworld lead.
class TestWorldMapNether : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    // Fixed seeds and one drawn per run (decision E-846).
    void theMapIsTheNetherTheServerOpens_data()
    {
        QTest::addColumn<qint64>("seed");
        QTest::addColumn<bool>("fortresses");
        QTest::newRow("seed 1") << qint64{1} << true;
        QTest::newRow("seed 20251028") << qint64{20251028} << true;
        QTest::newRow("random seed") << QcTestUtil::randomSeed() << false;
    }

    void theMapIsTheNetherTheServerOpens()
    {
        QFETCH(qint64, seed);
        QFETCH(bool, fortresses);
        const QcWorldMapRequest asked = request(seed, {0, 0, 1024, 64});
        const std::shared_ptr<const QcWorldGenerator> made = QcWorldMapWorker::dimensionGeneratorFor(asked);
        QVERIFY(made);
        const auto &preview = *qcDowncast<const QcNetherGenerator>(made.get());

        // The nether as the server opens it for a new world (QcDimensions::open): the content with
        // the world's options set, the shared factory, the version a new world stores for it.
        const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);
        const QcResult<QJsonObject> stored = QcWorldOptions::valuesToStore(terrain, asked.values, *m_content);
        QVERIFY(stored);
        const QcWorldOptions::Applied applied = QcWorldOptions::apply(m_content, terrain, *stored);
        QcWorldInfo info;
        info.generator = terrain;
        info = QcDimensions::withVersionsForNewWorld(*applied.content, info);
        const QcDimensionDefinition *nether = applied.content->worldgen().dimension(netherName);
        QVERIFY(nether);
        const std::unique_ptr<QcWorldGenerator> opened = QcWorldGenerators::createDimension(*nether, applied.content, seed, QcDimensions::versionIn(info, *nether));
        QVERIFY(opened);
        const QcWorldGenerator &world = *opened;
        QCOMPARE(preview.version(), world.version());

        checkBiomes(preview, world, asked.area);
        if (QTest::currentTestFailed()) {
            return;
        }

        // Every site stands exactly where /locate names it, and the fortress nearest to the middle
        // is among the sites when it lies in the square searched.
        const QcBiomeMap::Area searched = QcWorldMapWorker::sitesArea(asked.area);
        const std::optional<QList<QcBiomeMap::Site>> sites = QcBiomeMap::sitesOf(QcNetherMap::siteRules(preview), searched);
        QVERIFY(sites);
        qsizetype found = 0;
        qsizetype structures = 0;
        for (const QcBiomeMap::Site &site : *sites) {
            if (site.name != fortressKind) {
                ++structures;
                const std::optional<QcStructurePlacement> placed = world.nearestStructure(site.name, {site.x, 64, site.z}, 64);
                QVERIFY2(placed, qPrintable(site.name));
                const QcStructure &structure = placed->structure();
                const QcBlockPos named = placed->toWorld(QcBlockPos{structure.sizeX() / 2, structure.groundLayer + 1, structure.sizeZ() / 2});
                QCOMPARE(site.x, named.x);
                QCOMPARE(site.z, named.z);
                continue;
            }
            ++found;
            const QcLocateResult located = world.locate(u"fortress", {site.x, 64, site.z}, 64);
            QVERIFY(located.place);
            QCOMPARE(located.place->at.x, site.x);
            QCOMPARE(located.place->at.z, site.z);
        }
        qInfo("%lld fortresses of %lld sites", static_cast<long long>(found), static_cast<long long>(sites->size()));
        QVERIFY(structures > 0);
        if (fortresses) {
            QVERIFY(found > 0);
        }
        const QcLocateResult nearest = world.locate(u"fortress", {0, 64, 0}, 8 * searched.radius);
        QVERIFY(nearest.place);
        const QcBlockPos at = nearest.place->at;
        const bool inSquare = at.x >= -searched.radius && at.x < searched.radius && at.z >= -searched.radius && at.z < searched.radius;
        QCOMPARE(std::ranges::any_of(*sites, [at](const QcBiomeMap::Site &site) { return site.x == at.x && site.z == at.z; }), inSquare);
    }

    // The switch shows the nether with its biomes in the language and its sites; a new seed keeps
    // it; the spawn marker stands at the generator's spawn, in the nether at an eighth.
    void thePreviewShowsTheNetherAndKeepsTheChoice()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        QCOMPARE(preview.dimension(), overworldName);
        QVERIFY(preview.overworld());
        QCOMPARE(preview.coordinateScale(), 1.0);
        QCOMPARE(valuesOf(preview.dimensions()), (QStringList{overworldName, netherName}));
        QCOMPARE(preview.dimensions().at(1).toMap().value(u"text"_s).toString(), u"Nether"_s);
        QCOMPARE(preview.dimensions().at(0).toMap().value(u"text"_s).toString(), u"Oberwelt"_s);
        preview.setSeedText(u"1"_s);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);
        QVERIFY(!onlyNether(preview.legend()));
        const QImage overworld = preview.image();
        qInfo("overworld passes: coarse %lld ms, fine %lld ms", static_cast<long long>(preview.passMilliseconds()[0]),
              static_cast<long long>(preview.passMilliseconds()[1]));

        // The spawn of the world's generator.
        const QcBlockPos spawn = spawnOfSeed(u"1"_s);
        QVERIFY(checkSpawn(preview, spawn.x, spawn.z, u"World spawn"_s));

        const QSignalSpy changed(&preview, &QcWorldMapPreview::dimensionChanged);
        preview.setDimension(netherName);
        QCOMPARE(changed.count(), 1);
        QVERIFY(preview.busy());
        QVERIFY(preview.spawn().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);
        QVERIFY(preview.image() != overworld);
        QVERIFY(onlyNether(preview.legend()));
        QVERIFY(paintsLegendColours(preview));
        const QString centre = preview.placeAt(0.5, 0.5).value(u"name"_s).toString();
        QVERIFY2(isNetherBiomeName(centre), qPrintable(centre));
        QVERIFY(checkSpawn(preview, qcFloorDiv(spawn.x, 8), qcFloorDiv(spawn.z, 8), u"Portal from the world spawn"_s));

        // Another seed: still the nether, with the marker of the new world's spawn.
        preview.setSeedText(u"2"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);
        QCOMPARE(preview.dimension(), netherName);
        QCOMPARE(changed.count(), 1);
        QVERIFY(onlyNether(preview.legend()));
        const QcBlockPos otherSpawn = spawnOfSeed(u"2"_s);
        QVERIFY(otherSpawn.x != spawn.x || otherSpawn.z != spawn.z);
        QVERIFY(checkSpawn(preview, qcFloorDiv(otherSpawn.x, 8), qcFloorDiv(otherSpawn.z, 8), u"Portal from the world spawn"_s));

        // The sites, named in the language; the fortress first; back in the overworld other ones.
        preview.setSeedText(u"1"_s);
        preview.setShowSites(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && !preview.siteLegend().isEmpty(), 120000);
        qInfo("nether sites took %lld ms", static_cast<long long>(preview.passMilliseconds()[2]));
        const QVariantMap kind = preview.siteLegend().constFirst().toMap();
        QCOMPARE(kind.value(u"name"_s).toString(), fortressKind);
        QCOMPARE(kind.value(u"title"_s).toString(), m_content->translations().translate(u"structure.qtcraft.fortress"_s, u"de"_s));
        preview.setDimension(overworldName);
        QVERIFY(preview.sites().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && !preview.siteLegend().isEmpty(), 120000);
        QCOMPARE(preview.siteLegend().constFirst().toMap().value(u"name"_s).toString(), u"village"_s);
        QVERIFY(!onlyNether(preview.legend()));
    }

    // A change while the build is at the spawn: right after the fine map (no search then), or with
    // the search done and its result not yet handed over (dropped). The marker is the new one's.
    void aChangeAtTheSpawnStepLeavesNoOldMarker_data()
    {
        QTest::addColumn<QcWorldMapResult::Pass>("gate");
        QTest::addColumn<bool>("toTheNether");
        QTest::newRow("seed after the fine map") << QcWorldMapResult::Pass::Fine << false;
        QTest::newRow("seed after the search") << QcWorldMapResult::Pass::Spawn << false;
        QTest::newRow("dimension after the fine map") << QcWorldMapResult::Pass::Fine << true;
        QTest::newRow("dimension after the search") << QcWorldMapResult::Pass::Spawn << true;
    }

    void aChangeAtTheSpawnStepLeavesNoOldMarker()
    {
        QFETCH(QcWorldMapResult::Pass, gate);
        QFETCH(bool, toTheNether);
        QSemaphore reached;
        QSemaphore proceed;
        std::atomic<bool> first{true};
        std::atomic<int> spawnSteps{0};
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        preview.setResultHook([&](const QcWorldMapResult &result) {
            spawnSteps += result.pass == QcWorldMapResult::Pass::Spawn ? 1 : 0;
            if (result.pass == gate && first.exchange(false)) {
                reached.release();
                proceed.acquire();
            }
        });
        fixture.options.reset();
        preview.setSeedText(u"1"_s);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(reached.available() > 0, 60000);
        if (toTheNether) {
            preview.setDimension(netherName);
        } else {
            preview.setSeedText(u"2"_s);
        }
        proceed.release();
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);

        // After the fine map the old build ends before its search, so only the new one has the step.
        QCOMPARE(spawnSteps.load(), gate == QcWorldMapResult::Pass::Fine ? 1 : 2);
        if (toTheNether) {
            const QcBlockPos spawn = spawnOfSeed(u"1"_s);
            QVERIFY(checkSpawn(preview, qcFloorDiv(spawn.x, 8), qcFloorDiv(spawn.z, 8), u"Portal from the world spawn"_s));
        } else {
            const QcBlockPos spawn = spawnOfSeed(u"2"_s);
            QVERIFY(checkSpawn(preview, spawn.x, spawn.z, u"World spawn"_s));
        }
        preview.setActive(false);
    }

    // A nether block stands for eight of the overworld: the centre follows, the zoom stays; a
    // name that is no dimension with a map is refused.
    void theViewFollowsTheScale()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.pan(0.5, -0.25);
        QCOMPARE(preview.centreX(), QcWorldMapPreview::defaultRadius);
        QCOMPARE(preview.centreZ(), -QcWorldMapPreview::defaultRadius / 2);
        preview.setDimension(netherName);
        QCOMPARE(preview.coordinateScale(), 8.0);
        QVERIFY(!preview.overworld());
        QCOMPARE(preview.centreX(), QcWorldMapPreview::defaultRadius / 8);
        QCOMPARE(preview.centreZ(), -QcWorldMapPreview::defaultRadius / 16);
        QCOMPARE(preview.radius(), QcWorldMapPreview::defaultRadius);
        preview.setDimension(overworldName);
        QCOMPARE(preview.centreX(), QcWorldMapPreview::defaultRadius);
        QCOMPARE(preview.centreZ(), -QcWorldMapPreview::defaultRadius / 2);

        const QSignalSpy changed(&preview, &QcWorldMapPreview::dimensionChanged);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"dimension test:nowhere refused"_s));
        preview.setDimension(u"test:nowhere"_s);
        QCOMPARE(preview.dimension(), overworldName);
        QCOMPARE(changed.count(), 0);
    }

    // Back to the overworld when the shown dimension's mod is gone: the view moves to the same
    // place by the scales as a switch does, also when the content was released in between.
    void theFallbackToTheOverworldKeepsThePlace_data()
    {
        QTest::addColumn<bool>("released");
        QTest::newRow("new content") << false;
        QTest::newRow("released first") << true;
    }

    void theFallbackToTheOverworldKeepsThePlace()
    {
        QFETCH(bool, released);
        const std::shared_ptr<const QcContentRegistry> content = moddedContent();
        QcWorldMapPreview preview([] { return u"en"_s; });
        preview.setWorld(content, QcWorldOptions::defaults(*content));
        preview.setDimension(u"test:deep"_s);
        preview.pan(0.25, -0.125);
        const int centreX = preview.centreX();
        const int centreZ = preview.centreZ();
        QVERIFY(centreX != 0 && centreZ != 0);
        if (released) {
            preview.setWorld(nullptr, {});
        }
        preview.setWorld(m_content, QcWorldOptions::defaults(*m_content));
        QCOMPARE(preview.dimension(), overworldName);
        QCOMPARE(preview.coordinateScale(), 1.0);
        QCOMPARE(preview.centreX(), 2 * centreX);
        QCOMPARE(preview.centreZ(), 2 * centreZ);
    }

    // Content without dimensions/ plays the default overworld: its map shows with the world spawn.
    void contentWithoutDimensionsShowsItsOverworld()
    {
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        content->worldgen().removeDimension(overworldName);
        content->worldgen().removeDimension(netherName);
        QVERIFY(content->worldgen().dimensions().isEmpty());
        const QList<QcWorldMapResult> results = stepsOf(content, overworldName);
        QCOMPARE(passesOf(results), (QList{QcWorldMapResult::Pass::Coarse, QcWorldMapResult::Pass::Fine, QcWorldMapResult::Pass::Spawn}));
        QCOMPARE(results.at(2).spawn.kind, QcWorldMapSpawn::Kind::WorldSpawn);
    }

    // The marker of a dimension says what a portal at the world spawn does, as the server's links
    // decide it (QcPortalDefinition::arrivalIn): the scales of the overworld and the target, a
    // fixed arrival point, or nothing when no portal of the overworld leads there.
    void theSpawnMarkerFollowsThePortals_data()
    {
        using Portal = QcPortalDefinition;
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::addColumn<Portal::Link>("link");
        QTest::addColumn<Portal::Back>("back");
        QTest::addColumn<bool>("arrival");
        QTest::addColumn<int>("marker");
        const QString deep = u"test:deep"_s;
        const auto none = -1;
        const auto scaled = static_cast<int>(QcWorldMapSpawn::Kind::ScaledPortal);
        const auto fixed = static_cast<int>(QcWorldMapSpawn::Kind::FixedArrival);
        QTest::newRow("no portal") << QString() << QString() << Portal::Link::Scaled << Portal::Back::Linked << false << none;
        QTest::newRow("scaled") << overworldName << deep << Portal::Link::Scaled << Portal::Back::Linked << false << scaled;
        QTest::newRow("fixed") << overworldName << deep << Portal::Link::Fixed << Portal::Back::Linked << true << fixed;
        QTest::newRow("fixed without a point") << overworldName << deep << Portal::Link::Fixed << Portal::Back::Linked << false << scaled;
        QTest::newRow("scaled beside a point") << overworldName << deep << Portal::Link::Scaled << Portal::Back::Linked << true << scaled;
        QTest::newRow("from another dimension") << netherName << deep << Portal::Link::Scaled << Portal::Back::Linked << false << none;
        QTest::newRow("the way back") << deep << overworldName << Portal::Link::Fixed << Portal::Back::Linked << false << scaled;
        QTest::newRow("back to the spawn") << deep << overworldName << Portal::Link::Scaled << Portal::Back::Spawn << false << none;
    }

    void theSpawnMarkerFollowsThePortals()
    {
        using Portal = QcPortalDefinition;
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(Portal::Link, link);
        QFETCH(Portal::Back, back);
        QFETCH(bool, arrival);
        QFETCH(int, marker);

        // A block of the overworld stands for 6, one of the dimension for 2: a scaled link takes a
        // column to three times its number.
        std::shared_ptr<QcContentRegistry> content = moddedContent();
        QcDimensionDefinition overworld = content->worldgen().overworld();
        overworld.coordinateScale = 6.0;
        content->worldgen().registerDimension(std::move(overworld));
        QcDimensionDefinition deep = *content->worldgen().dimension(u"test:deep"_s);
        if (arrival) {
            deep.arrival = QcDimensionDefinition::Arrival{{40, 64, -30}, {}, 2};
        }
        content->worldgen().registerDimension(std::move(deep));
        if (!from.isEmpty()) {
            Portal portal;
            portal.name = u"test:gate"_s;
            portal.from = from;
            portal.to = to;
            portal.link = link;
            portal.back = back;
            content->worldgen().registerPortal(std::move(portal));
        }

        const QList<QcWorldMapResult> results = stepsOf(content, u"test:deep"_s);
        const auto step = std::ranges::find(results, QcWorldMapResult::Pass::Spawn, &QcWorldMapResult::pass);
        QCOMPARE(step != results.end(), marker >= 0);
        if (marker < 0) {
            return;
        }
        QCOMPARE(static_cast<int>(step->spawn.kind), marker);
        const QcBlockPos spawn = spawnOfSeed(u"1"_s, content);
        const QPoint expected = step->spawn.kind == QcWorldMapSpawn::Kind::FixedArrival ? QPoint(40, -30) : QPoint(3 * spawn.x, 3 * spawn.z);
        QCOMPARE(step->spawn.column, expected);
    }

    // A turned structure of even size, whose middle is not the middle of its turned footprint,
    // stands on the map exactly where /locate names it.
    void aTurnedStructureStandsWhereLocateNamesIt()
    {
        const std::shared_ptr<const QcContentRegistry> content = moddedContent();
        QcWorldMapRequest asked{content, QcWorldOptions::defaults(*content), 1, {0, 0, 1024, 64}, false};
        asked.dimension = netherName;
        const std::shared_ptr<const QcWorldGenerator> made = QcWorldMapWorker::dimensionGeneratorFor(asked);
        QVERIFY(made);
        const std::optional<QList<QcBiomeMap::Site>> sites =
            QcBiomeMap::sitesOf(QcNetherMap::siteRules(*qcDowncast<const QcNetherGenerator>(made.get())), QcWorldMapWorker::sitesArea(asked.area));
        QVERIFY(sites);
        qsizetype turned = 0;
        for (const QcBiomeMap::Site &site : *sites) {
            if (site.name != evenRuin) {
                continue;
            }
            const std::optional<QcStructurePlacement> placed = made->nearestStructure(site.name, {site.x, 64, site.z}, 64);
            QVERIFY(placed);
            const QcStructure &structure = placed->structure();
            QCOMPARE(structure.sizeX() % 2, 0);
            const QcBlockPos named = placed->toWorld(QcBlockPos{structure.sizeX() / 2, structure.groundLayer + 1, structure.sizeZ() / 2});
            QCOMPARE(site.x, named.x);
            QCOMPARE(site.z, named.z);
            turned += placed->turns() != 0 ? 1 : 0;
        }
        QVERIFY(turned > 0);
    }

    // A kind of site without a structure file is named by its key "structure.<pack>.<name>".
    void theFortressIsNamedByItsKey()
    {
        const QcWorldMapTexts german(m_content.get(), u"de"_s);
        QCOMPARE(german.kind(fortressKind), u"Netherfestung"_s);
        const QcWorldMapTexts english(m_content.get(), u"en"_s);
        QCOMPARE(english.kind(fortressKind), u"Nether fortress"_s);
        QCOMPARE(english.kind(u"qtcraft:no_such_thing"_s), u"no_such_thing"_s);
    }

    // What a mod adds comes without code: a dimension on the nether's generator with its own
    // scale joins the switch (by its bare name without a text), one on a generator without a map
    // stays out, a biome shows in its colour, a structure of the file as a kind of its own.
    void aModsDimensionBiomeAndStructureShow()
    {
        const std::shared_ptr<const QcContentRegistry> content = moddedContent();
        const QcDimensionDefinition *flat = content->worldgen().dimension(u"test:flatland"_s);
        QVERIFY(flat && !QcDimensionMaps::hasMap(*flat, *content));
        const QcDimensionDefinition *refused = content->worldgen().dimension(u"test:refused"_s);
        QVERIFY(refused && !QcDimensionMaps::hasMap(*refused, *content));
        QcWorldMapPreview preview([] { return u"en"_s; });
        preview.setWorld(content, QcWorldOptions::defaults(*content));
        QCOMPARE(valuesOf(preview.dimensions()), (QStringList{overworldName, netherName, u"test:deep"_s}));
        QCOMPARE(preview.dimensions().at(2).toMap().value(u"text"_s).toString(), u"deep"_s);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"dimension test:flatland refused"_s));
        preview.setDimension(u"test:flatland"_s);

        preview.setSeedText(u"1"_s);
        preview.setShowSites(true);
        preview.setDimension(u"test:deep"_s);
        QCOMPARE(preview.coordinateScale(), 2.0);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 120000);
        // No portal leads there.
        QVERIFY(preview.spawn().isEmpty());

        const QVariantList legend = preview.legend();
        const auto ash = std::ranges::find_if(legend, [](const QVariant &entry) { return entry.toMap().value(u"name"_s) == u"test:ash"_s; });
        QVERIFY2(ash != legend.end(), "the mod's biome is not on the map");
        QCOMPARE(ash->toMap().value(u"colour"_s).value<QColor>(), QColor(0xee, 0xee, 0x11));
        QStringList kinds;
        const QVariantList sites = preview.siteLegend();
        for (const QVariant &site : sites) {
            kinds.append(site.toMap().value(u"name"_s).toString());
        }
        QVERIFY2(kinds.contains(u"ruined_portal"_s), qPrintable(kinds.join(u", "_s)));

        // The mod is gone: its dimension is, too.
        preview.setWorld(m_content, QcWorldOptions::defaults(*m_content));
        QCOMPARE(preview.dimension(), overworldName);
        preview.setActive(false);
    }

private:
    // Every cell of the map of `area` is the biome `world` has at its column, and a nether biome.
    void checkBiomes(const QcNetherGenerator &preview, const QcWorldGenerator &world, QcBiomeMap::Area area) const
    {
        const std::optional<QcBiomeMap> map = QcNetherMap::sample(preview, *m_content, area);
        QVERIFY(map);
        QSet<QString> seen;
        for (int z = 0; z < map->size(); ++z) {
            for (int x = 0; x < map->size(); ++x) {
                const QcBiomeMap::Cell &cell = map->cell(x, z);
                QVERIFY(cell.biome >= 0 && cell.water == QcBiomeMap::Water::Land);
                const QcBiomeDefinition &biome = m_content->worldgen().biomes().at(cell.biome);
                QCOMPARE(biome.name, world.biomeAt(area.blockX(x), area.blockZ(z)));
                QCOMPARE(biome.dimension, netherName);
                seen.insert(biome.name);
            }
        }
        QVERIFY2(seen.size() >= 2, qPrintable(u"%1 biomes"_s.arg(seen.size())));
    }

    struct Fixture
    {
        Fixture()
            : options(library, [] { return u"de"_s; })
        {
            library.setModsDirectory({});
        }

        QcWorldLibraryController library;
        QcWorldOptionsController options;
    };

    QcWorldMapRequest request(qint64 seed, QcBiomeMap::Area area) const
    {
        QcWorldMapRequest asked{m_content, QcWorldOptions::defaults(*m_content), seed, area, false};
        asked.dimension = netherName;
        return asked;
    }

    static QStringList valuesOf(const QVariantList &dimensions)
    {
        QStringList values;
        for (const QVariant &dimension : dimensions) {
            values.append(dimension.toMap().value(u"value"_s).toString());
        }
        return values;
    }

    // The spawn of the overworld of a world of `content` with the seed `text`.
    static QcBlockPos spawnOfSeed(const QString &text, const std::shared_ptr<const QcContentRegistry> &content)
    {
        const QcWorldMapRequest world{content, QcWorldOptions::defaults(*content), QcWorldLibrary::seedFromText(text), {}, false};
        return QcWorldMapWorker::generatorFor(world)->spawnPoint();
    }

    // The same with the base content.
    QcBlockPos spawnOfSeed(const QString &text) const { return spawnOfSeed(text, m_content); }

    // The steps of a small map of `dimension` of `content` with the seed "1".
    static QList<QcWorldMapResult> stepsOf(const std::shared_ptr<const QcContentRegistry> &content, const QString &dimension)
    {
        QcWorldMapRequest asked{content, QcWorldOptions::defaults(*content), QcWorldLibrary::seedFromText(u"1"_s), {0, 0, 64, 1}, false};
        asked.dimension = dimension;
        QcWorldMapWorker worker;
        QList<QcWorldMapResult> results;
        worker.run(asked, 1, [] { return false; }, [&results](QcWorldMapResult result) { results.append(std::move(result)); });
        return results;
    }

    static QList<QcWorldMapResult::Pass> passesOf(const QList<QcWorldMapResult> &results)
    {
        QList<QcWorldMapResult::Pass> passes;
        for (const QcWorldMapResult &result : results) {
            passes.append(result.pass);
        }
        return passes;
    }

    // Whether every cell of the shown map has exactly a colour of its legend.
    static bool paintsLegendColours(const QcWorldMapPreview &preview)
    {
        QSet<QRgb> colours;
        const QVariantList legend = preview.legend();
        for (const QVariant &entry : legend) {
            colours.insert(entry.toMap().value(u"colour"_s).value<QColor>().rgb());
        }
        const QImage &image = preview.image();
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (!colours.contains(image.pixel(x, y))) {
                    return false;
                }
            }
        }
        return !image.isNull();
    }

    // Whether the marker of `preview` stands at the column (x, z) of the shown map under `title`.
    static bool checkSpawn(const QcWorldMapPreview &preview, int x, int z, const QString &title)
    {
        const QVariantMap spawn = preview.spawn();
        const double width = 2.0 * preview.radius();
        const double expectedX = (x - (preview.centreX() - preview.radius())) / width;
        const double expectedY = (z - (preview.centreZ() - preview.radius())) / width;
        return spawn.value(u"title"_s).toString() == title && qFuzzyCompare(spawn.value(u"x"_s).toDouble(), expectedX) &&
               qFuzzyCompare(spawn.value(u"y"_s).toDouble(), expectedY) && spawn.value(u"text"_s).toString().contains(QString::number(x));
    }

    // The content with what a data-only mod could add.
    static std::shared_ptr<QcContentRegistry> moddedContent()
    {
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        const QJsonObject deep{{u"name"_s, u"test:deep"_s}, {u"generator"_s, QJsonObject{{u"type"_s, u"nether"_s}}}, {u"coordinateScale"_s, 2}, {u"ceiling"_s, 127}};
        const QJsonObject flatland{{u"name"_s, u"test:flatland"_s}, {u"generator"_s, QJsonObject{{u"type"_s, u"flat"_s}}}};
        // The server refuses it: the nether's generator takes no options.
        const QJsonObject refused{{u"name"_s, u"test:refused"_s}, {u"generator"_s, QJsonObject{{u"type"_s, u"nether"_s}, {u"options"_s, u"odd"_s}}}};
        for (const QJsonObject &json : {deep, flatland, refused}) {
            QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
            if (!dimension) {
                qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
            }
            content->worldgen().registerDimension(std::move(*dimension));
        }

        // A second biome at the nether wastes' point of the map would never win: it takes a corner.
        QcBiomeDefinition ash = *content->worldgen().biome(u"qtcraft:nether_wastes"_s);
        ash.name = u"test:ash"_s;
        ash.translationKey = u"biome.test.ash"_s;
        ash.temperature = 0.0;
        ash.humidity = 1.0;
        ash.mapColour = 0xeeee11;
        content->worldgen().registerBiome(std::move(ash));

        QcNetherSettings nether = content->worldgen().nether();
        QcNetherSettings::Structure portal = nether.structures.constFirst();
        portal.structure = u"qtcraft:ruined_portal"_s;
        portal.spacing = 128;
        portal.chance = 1.0;
        nether.structures.append(portal);

        const QJsonArray rows{u"nnnn"_s, u"nnnn"_s, u"nnnn"_s, u"nnnn"_s};
        const QJsonObject even{{u"name"_s, evenRuin}, {u"palette"_s, QJsonObject{{u"n"_s, u"qtcraft:netherrack"_s}}}, {u"layers"_s, QJsonArray{rows, rows}}};
        QcResult<QcStructureDefinition> structure = QcStructureParser::parseStructure(even, u"test"_s);
        if (!structure) {
            qFatal("test structure does not parse: %s", qPrintable(structure.error()));
        }
        content->worldgen().registerStructure(std::move(*structure));
        portal.structure = evenRuin;
        nether.structures.append(portal);
        content->worldgen().setNether(std::move(nether));
        return content;
    }

    // Whether `name` is the German name of a nether biome.
    bool isNetherBiomeName(const QString &name) const
    {
        const QList<QcBiomeDefinition> &biomes = m_content->worldgen().biomes();
        return std::ranges::any_of(biomes, [&](const QcBiomeDefinition &biome) {
            return biome.dimension == netherName &&
                   m_content->translations().translate(biome.translationKey, u"de"_s) == name;
        });
    }

    // Whether every entry of `legend` names a nether biome.
    bool onlyNether(const QVariantList &legend) const
    {
        return !legend.isEmpty() &&
               std::ranges::all_of(legend, [this](const QVariant &entry) { return isNetherBiomeName(entry.toMap().value(u"name"_s).toString()); });
    }

    QTemporaryDir m_data;
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
};

QTEST_GUILESS_MAIN(TestWorldMapNether)
#include "test_world_map_nether.moc"
