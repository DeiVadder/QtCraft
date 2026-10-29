// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldLibraryController.h"
#include "app/controllers/QcWorldOptionsController.h"
#include "app/preview/QcWorldMapPainter.h"
#include "app/preview/QcWorldMapPreview.h"
#include "app/preview/QcWorldMapWork.h"
#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcStructureDefinition.h"
#include "common/core/QcDowncast.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worlds/QcWorldLibrary.h"

#include <QElapsedTimer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

// The map preview of a new world (decision E-921): the same seed and options give the same map,
// the map shows exactly the biomes and water of the world the server then generates, a change
// stops the running computation, and the legend comes from the content in the player's language.
class TestWorldMapPreview : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    void sameSeedAndOptionsGiveTheSameMap()
    {
        const QcBiomeMap::Area area{0, 0, 512, 16};
        const QImage first = drawn(request(7, defaults(), area));
        QVERIFY(!first.isNull());
        QCOMPARE(drawn(request(7, defaults(), area)), first);
        QVERIFY(drawn(request(8, defaults(), area)) != first);

        QJsonObject larger = defaults();
        larger.insert(u"qtcraft:biome_size"_s, 2.0);
        QVERIFY(drawn(request(7, larger, area)) != first);
    }

    // Fixed seeds and one drawn per run (decision E-846); every cell lies in its own chunk.
    void theMapMatchesGeneratedChunks_data()
    {
        QTest::addColumn<qint64>("seed");
        QTest::addColumn<double>("biomeSize");
        QTest::newRow("seed 1") << qint64{1} << 1.0;
        QTest::newRow("seed 20251028, small biomes") << qint64{20251028} << 0.5;
        // 82413031 and 102758670: windows almost all of mountains, whose overhangs move the ground
        // (decision E-926).
        QTest::newRow("seed 82413031, mountains") << qint64{82413031} << 1.0;
        QTest::newRow("seed 102758670, mountains") << qint64{102758670} << 1.0;
        QTest::newRow("random seed") << QcTestUtil::randomSeed() << 1.0;
    }

    void theMapMatchesGeneratedChunks()
    {
        QFETCH(qint64, seed);
        QFETCH(double, biomeSize);
        QJsonObject values = defaults();
        values.insert(u"qtcraft:biome_size"_s, biomeSize);
        const QcWorldMapRequest asked = request(seed, values, {0, 0, 256, 32});
        const std::shared_ptr<const QcTerrainGenerator> preview = QcWorldMapWorker::generatorFor(asked);
        QVERIFY(preview);
        const std::optional<QcBiomeMap> map = QcBiomeMap::sample(*preview, asked.area);
        QVERIFY(map);

        // The world as the game starts it (QcWorldStarter): the stored values set into the content,
        // the generator from the factory.
        const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);
        const QcResult<QJsonObject> stored = QcWorldOptions::valuesToStore(terrain, values, *m_content);
        QVERIFY(stored);
        const QcWorldOptions::Applied applied = QcWorldOptions::apply(m_content, terrain, *stored);
        const std::unique_ptr<QcWorldGenerator> world = QcWorldGenerators::create(terrain, applied.content, seed, {}, QcGeneratorVersion::current);
        const auto &generator = *qcDowncast<const QcTerrainGenerator>(world.get());

        // Chunks keep no biome: the biome is compared with the world's own generator (made on the
        // world's path, not the preview's), water, ice and the height of the land with the blocks of
        // the really generated chunks.
        Agreement water;
        Agreement ice;
        Agreement ground;
        Agreement exactPlainGround;
        Agreement exactOverhangGround;
        for (int z = 0; z < map->size(); ++z) {
            for (int x = 0; x < map->size(); ++x) {
                const int blockX = asked.area.blockX(x);
                const int blockZ = asked.area.blockZ(z);
                const QcBiomeMap::Cell &cell = map->cell(x, z);
                QCOMPARE(preview->biomeName(cell.biome), generator.biomeAt(blockX, blockZ));

                const std::shared_ptr<QcChunk> chunk = generator.generate(QcBlockPos{blockX, 0, blockZ}.chunk());
                const auto blockAt = [&](int y) -> const QcBlockDefinition & {
                    const QcBlockPos at{blockX, y, blockZ};
                    return applied.content->block(chunk->block(at.localX(), at.y, at.localZ()));
                };
                const QString top = blockAt(generator.seaLevel() - 1).name;
                water.add(isWater(top) == (cell.water != QcBiomeMap::Water::Land));
                if (cell.water != QcBiomeMap::Water::Land) {
                    ice.add(top.endsWith(u"ice"_s) == cell.frozen);
                }
                if (cell.water == QcBiomeMap::Water::Land) {
                    int groundTop = cell.height + groundSlack;
                    while (groundTop > cell.height - groundSlack && !isGround(blockAt(groundTop))) {
                        --groundTop;
                    }
                    Agreement &exact = generator.column(blockX, blockZ).overhang < 1.0 ? exactPlainGround : exactOverhangGround;
                    exact.add(std::abs(groundTop + 1 - cell.height) <= 1);
                    ground.add(std::abs(groundTop + 1 - cell.height) <= groundSlack / 2);
                }
            }
        }
        // Lakes on the surface, wetlands and caves at the sea level change a few columns after the
        // terrain (QcSurfaceLakes, QcWetlandShaper, QcCaveCarver); the map shows the terrain. Its
        // height is the column's before the shaping of mountains (overhangs, cliffs), which moves
        // the ground there by up to about 5 blocks: only about 60 % of the columns with overhangs
        // match to a block (51 % on the worst of 84 seeds), so they count to 6 blocks and to a
        // block only loosely. Columns without overhangs matched to a block in 99.4 % (96.7 % on
        // the worst seed; decision E-926).
        qInfo("water agrees in %s, ice in %s, the height to a block of land without overhangs in %s, with overhangs in %s, "
              "of all land to %d blocks in %s cells",
              qPrintable(water.text()), qPrintable(ice.text()), qPrintable(exactPlainGround.text()), qPrintable(exactOverhangGround.text()),
              groundSlack / 2, qPrintable(ground.text()));
        QVERIFY2(water.atLeast(97), qPrintable(water.text()));
        QVERIFY2(ice.atLeast(97), qPrintable(ice.text()));
        QVERIFY2(ground.atLeast(97), qPrintable(ground.text()));
        // A share is checked only on enough cells; without overhangs at most 5 % of them (at least
        // one) may miss.
        if (exactPlainGround.cells >= minCompared) {
            QVERIFY2(exactPlainGround.misses() <= std::max(1, exactPlainGround.cells * 5 / 100), qPrintable(exactPlainGround.text()));
        }
        if (exactOverhangGround.cells >= minCompared) {
            QVERIFY2(exactOverhangGround.atLeast(40), qPrintable(exactOverhangGround.text()));
        }
    }

    // The worker stops between rows as soon as it is told: nothing more is handed over.
    void theWorkerStopsWhenCancelled()
    {
        QcWorldMapWorker worker;
        const QcWorldMapRequest asked = request(3, defaults(), {0, 0, 1024, 1});
        QList<QcWorldMapResult::Pass> passes;
        int asks = 0;
        // The coarse pass has 64 rows: stop during the fine one.
        worker.run(asked, 1, [&asks] { return ++asks > 100; }, [&passes](const QcWorldMapResult &result) { passes.append(result.pass); });
        QCOMPARE(passes, QList{QcWorldMapResult::Pass::Coarse});

        passes.clear();
        asks = 0;
        worker.run(asked, 2, [&asks] { return ++asks > 10; }, [&passes](const QcWorldMapResult &result) { passes.append(result.pass); });
        QVERIFY(passes.isEmpty());
    }

    // A change while the preview computes stops that build at once: its map, finished but not yet
    // handed over, never shows, and only the new one's map does. Measures the passes in the log
    // (docs/Performance-Verlauf.md).
    void aChangeCancelsTheRunningComputation()
    {
        // The first result of the first build waits on the worker until the change is made.
        QSemaphore reached;
        QSemaphore proceed;
        std::atomic<bool> first{true};
        QList<QImage> shown;
        QStringList shownCentres;
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        preview.setResultHook([&](const QcWorldMapResult &) {
            if (first.exchange(false)) {
                reached.release();
                proceed.acquire();
            }
        });
        connect(&preview, &QcWorldMapPreview::imageChanged, &preview, [&] {
            if (preview.stage() != QcWorldMapPreview::Stage::Empty) {
                shown.append(preview.image());
                shownCentres.append(preview.placeAt(0.5, 0.5).value(u"name"_s).toString());
            }
        });
        fixture.options.reset();
        preview.setSeedText(u"1"_s);
        preview.setShowSites(true);
        preview.setActive(true);

        QTRY_VERIFY_WITH_TIMEOUT(reached.available() > 0, 60000);
        preview.setSeedText(u"2"_s);
        QVERIFY(preview.busy());
        proceed.release();
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.passMilliseconds()[2] >= 0, 120000);
        QCOMPARE(preview.cancelledRuns(), 1);
        const std::array<qint64, 3> passes = preview.passMilliseconds();
        qInfo("map preview, debug: coarse %lld ms (with the generator), fine %lld ms, sites %lld ms", passes[0], passes[1], passes[2]);

        const QcBiomeMap::Area view{preview.centreX(), preview.centreZ(), preview.radius(), 1};
        const auto asked = [&](int cells) {
            return request(QcWorldLibrary::seedFromText(u"2"_s), QJsonObject::fromVariantMap(fixture.options.values()),
                           QcWorldMapWorker::withCells(view, cells));
        };
        QCOMPARE(shown.size(), 2);
        QCOMPARE(shown.first(), drawn(asked(QcWorldMapWorker::coarseCells)));
        // The cells under the pointer are the new map's too: the old raster went with its build.
        QCOMPARE(shownCentres.first(), nameAt(asked(QcWorldMapWorker::coarseCells), 0.5, 0.5));
        QCOMPARE(preview.image(), drawn(asked(QcWorldMapWorker::fineCells)));
    }

    // Switching the villages and structures on plans only them when the map is current; off, the
    // markers just go. While the view moves they stay where they are until the new ones come.
    void theSitesSwitchKeepsTheMap()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.setSeedText(u"1"_s);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);

        const QSignalSpy images(&preview, &QcWorldMapPreview::imageChanged);
        preview.setShowSites(true);
        QVERIFY(preview.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy(), 120000);
        QCOMPARE(images.count(), 0);
        QVERIFY(preview.passMilliseconds()[2] >= 0);
        const qsizetype found = preview.sites().size();
        QVERIFY(found > 0);

        preview.setShowSites(false);
        QVERIFY(!preview.busy());
        QVERIFY(preview.sites().isEmpty());

        preview.setShowSites(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.sites().size() == found, 120000);
        preview.pan(0.25, 0.0);
        QVERIFY(preview.busy());
        QCOMPARE(preview.sites().size(), found);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy(), 120000);
        QVERIFY(!preview.sites().isEmpty());
    }

    // The progress is the running build's (decision E-930), not the shown image's: a change sets it
    // to 0 while the old map still shows, each pass adds its share, the sites alone start after
    // the map's two passes.
    void progressFollowsTheRunningBuild()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.setSeedText(u"1"_s);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);
        QCOMPARE(preview.progress(), 1.0);

        QList<double> seen;
        connect(&preview, &QcWorldMapPreview::progressChanged, &preview, [&] { seen.append(preview.progress()); });
        preview.setSeedText(u"2"_s);
        QVERIFY(preview.busy());
        QCOMPARE(preview.stage(), QcWorldMapPreview::Stage::Fine);
        QCOMPARE(preview.progress(), 0.0);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy(), 60000);
        QCOMPARE(seen, (QList<double>{0.0, 0.5, 1.0}));

        seen.clear();
        preview.setShowSites(true);
        QVERIFY(preview.busy());
        QCOMPARE(preview.progress(), 2.0 / 3.0);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy(), 120000);
        QCOMPARE(preview.progress(), 1.0);

        preview.setActive(false);
        fixture.options.release();
        QCOMPARE(preview.progress(), 0.0);
    }

    // Released (a world starts) or hidden, the preview lets go of the menu's content: its worker
    // drops the generator and the content copy it was made from.
    void releaseLetsGoOfTheContent()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.setSeedText(u"4"_s);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);

        const std::shared_ptr<const QcContentRegistry> menu = fixture.library.loadedContent();
        QVERIFY(menu);
        const long shownUses = menu.use_count();
        preview.setActive(false);
        QTRY_VERIFY(menu.use_count() < shownUses);

        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && menu.use_count() == shownUses, 60000);
        fixture.options.release();
        // Only the library and this test hold it now.
        QTRY_COMPARE(menu.use_count(), 2L);
    }

    // Hidden, the preview computes nothing; shown again, it does.
    void onlyAnActivePreviewComputes()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.setSeedText(u"5"_s);
        QTest::qWait(3 * QcWorldMapPreview::settleMs);
        QVERIFY(!preview.busy());
        QCOMPARE(preview.stage(), QcWorldMapPreview::Stage::Empty);

        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);
        fixture.options.release();
        QCOMPARE(preview.stage(), QcWorldMapPreview::Stage::Empty);
        QVERIFY(preview.legend().isEmpty());
    }

    // The legend: the content's biome names in the player's language, the shares adding up to the
    // whole map, the sea among them.
    void theLegendNamesTheBiomesInTheLanguage()
    {
        Fixture fixture(u"de"_s);
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.setSeedText(u"1"_s);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine, 60000);

        double total = 0.0;
        QStringList names;
        const QVariantList legend = preview.legend();
        for (const QVariant &entry : legend) {
            total += entry.toMap().value(u"share"_s).toDouble();
            names.append(entry.toMap().value(u"name"_s).toString());
        }
        QCOMPARE_LE(std::abs(total - 1.0), 1e-9);
        QVERIFY2(names.contains(u"Meer und Seen"_s) || names.contains(u"Ebene"_s), qPrintable(names.join(u", "_s)));
        QVERIFY(!names.contains(u"biome.qtcraft.plains"_s));
    }

    // Every kind of site has its legend entry, named from the content in the player's language,
    // and its box hides or shows just its markers, without computing anything.
    void theSiteKindsAreNamedAndFiltered()
    {
        Fixture fixture(u"de"_s);
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.setSeedText(u"1"_s);
        preview.setShowSites(true);
        preview.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && !preview.siteLegend().isEmpty(), 120000);

        QHash<QString, int> counts;
        int total = 0;
        const QVariantList legend = preview.siteLegend();
        for (const QVariant &entry : legend) {
            const QVariantMap kind = entry.toMap();
            const QString name = kind.value(u"name"_s).toString();
            const QString title = kind.value(u"title"_s).toString();
            QVERIFY(kind.value(u"shown"_s).toBool());
            counts.insert(name, kind.value(u"count"_s).toInt());
            total += counts.value(name);
            const QcStructureDefinition *structure = m_content->worldgen().structure(name.contains(u':') ? name : u"qtcraft:"_s + name);
            if (structure) {
                QCOMPARE(title, m_content->translations().translate(structure->translationKey, u"de"_s));
            }
            QVERIFY2(!title.contains(u':') && !title.isEmpty(), qPrintable(title));
        }
        QCOMPARE(legend.constFirst().toMap().value(u"name"_s).toString(), u"village"_s);
        QVERIFY2(counts.size() >= 3 && counts.contains(u"qtcraft:witch_hut"_s), qPrintable(u"%1 kinds"_s.arg(counts.size())));
        // The view is the square searched: every site found is on the map.
        QCOMPARE(preview.sites().size(), total);

        const QString hidden = u"qtcraft:witch_hut"_s;
        preview.setKindShown(hidden, false);
        QVERIFY(!preview.busy());
        QCOMPARE(preview.sites().size(), total - counts.value(hidden));
        const QVariantList markers = preview.sites();
        for (const QVariant &marker : markers) {
            QVERIFY(marker.toMap().value(u"name"_s).toString() != hidden);
        }
        const auto shownOf = [&preview](const QString &kind) {
            const QVariantList kinds = preview.siteLegend();
            const auto entry = std::ranges::find_if(kinds, [&kind](const QVariant &each) { return each.toMap().value(u"name"_s) == kind; });
            return entry->toMap().value(u"shown"_s).toBool();
        };
        QVERIFY(!shownOf(hidden));
        preview.setKindShown(hidden, true);
        QCOMPARE(preview.sites().size(), total);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"kind nothing refused"_s));
        preview.setKindShown(u"nothing"_s, false);

        // The main switch shows every kind again.
        preview.setKindShown(hidden, false);
        preview.setShowSites(false);
        QVERIFY(preview.siteLegend().isEmpty());
        preview.setShowSites(true);
        QTRY_VERIFY_WITH_TIMEOUT(!preview.busy() && preview.sites().size() == total, 120000);
        QVERIFY(shownOf(hidden));
    }

    // The pointer over the map names the biome or sea of the shown map's cell and the block column
    // under it, at any zoom and place; the line for the keyboard names the centre.
    void placeAtNamesTheCellAndItsColumn()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        fixture.options.reset();
        preview.setSeedText(u"1"_s);
        QVERIFY(preview.placeAt(0.5, 0.5).isEmpty());
        preview.setActive(true);
        const auto settled = [&preview] { return !preview.busy() && preview.stage() == QcWorldMapPreview::Stage::Fine; };
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 60000);

        for (int view = 0; view < 2; ++view) {
            const QcBiomeMap::Area area{preview.centreX(), preview.centreZ(), preview.radius(), 1};
            const QcWorldMapRequest asked = request(QcWorldLibrary::seedFromText(u"1"_s), QJsonObject::fromVariantMap(fixture.options.values()),
                                                    QcWorldMapWorker::withCells(area, QcWorldMapWorker::fineCells));
            for (const auto &[x, y] : {std::pair{0.1, 0.2}, std::pair{0.5, 0.5}, std::pair{0.93, 0.71}, std::pair{0.0, 0.999}}) {
                const QVariantMap place = preview.placeAt(x, y);
                QCOMPARE(place.value(u"name"_s).toString(), nameAt(asked, x, y));
                QCOMPARE(place.value(u"x"_s).toInt(), static_cast<int>(std::floor(area.centreX - area.radius + x * 2 * area.radius)));
                QCOMPARE(place.value(u"z"_s).toInt(), static_cast<int>(std::floor(area.centreZ - area.radius + y * 2 * area.radius)));
            }
            QCOMPARE(preview.placeAt(0.0, 0.0).value(u"x"_s).toInt(), area.centreX - area.radius);
            QVERIFY(preview.placeAt(1.0, 0.5).isEmpty());
            QCOMPARE(preview.centreText(),
                     u"%1, x %2, z %3"_s.arg(nameAt(asked, 0.5, 0.5)).arg(area.centreX).arg(area.centreZ));

            // Nearer and elsewhere.
            preview.zoomIn();
            preview.zoomIn();
            preview.pan(0.3, -0.45);
            QTRY_VERIFY_WITH_TIMEOUT(settled(), 60000);
        }
    }

    void viewChangesAreChecked()
    {
        Fixture fixture;
        QcWorldMapPreview &preview = *fixture.options.preview();
        QCOMPARE(preview.radius(), QcWorldMapPreview::defaultRadius);
        while (preview.canZoomOut()) {
            preview.zoomOut();
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"cannot zoom out"_s));
        preview.zoomOut();
        QCOMPARE(preview.radius(), QcWorldMapPreview::radii.back());
        preview.pan(0.5, -0.25);
        QCOMPARE(preview.centreX(), QcWorldMapPreview::radii.back());
        QCOMPARE(preview.centreZ(), -QcWorldMapPreview::radii.back() / 2);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"pan by .* refused"_s));
        preview.pan(std::nan(""), 0.0);
        preview.resetView();
        QCOMPARE(preview.centreX(), 0);
    }

private:
    struct Fixture
    {
        explicit Fixture(const QString &language = u"en"_s)
            : options(library, [language] { return language; })
        {
            library.setModsDirectory({});
        }

        QcWorldLibraryController library;
        QcWorldOptionsController options;
    };

    QJsonObject defaults() const { return QcWorldOptions::defaults(*m_content); }

    QcWorldMapRequest request(qint64 seed, const QJsonObject &values, QcBiomeMap::Area area) const
    {
        return {m_content, values, seed, area, false};
    }

    // The map of `asked` as the preview draws it.
    static QImage drawn(const QcWorldMapRequest &asked)
    {
        const std::shared_ptr<const QcTerrainGenerator> generator = QcWorldMapWorker::generatorFor(asked);
        const std::optional<QcBiomeMap> map = QcBiomeMap::sample(*generator, asked.area);
        return QcWorldMapPainter::paint(*map, QcWorldMapPainter::biomeColours(*asked.content), generator->seaLevel());
    }

    // What the legend calls the cell of `asked`'s map at (x, y), in English.
    QString nameAt(const QcWorldMapRequest &asked, double x, double y) const
    {
        const std::shared_ptr<const QcTerrainGenerator> generator = QcWorldMapWorker::generatorFor(asked);
        const QcBiomeMap::Cell cell = QcBiomeMap::cellAt(*generator, asked.area.blockX(static_cast<int>(x * asked.area.cells())),
                                                         asked.area.blockZ(static_cast<int>(y * asked.area.cells())));
        if (cell.water == QcBiomeMap::Water::Sea) {
            return cell.frozen ? u"Frozen sea and lakes"_s : u"Sea and lakes"_s;
        }
        return m_content->translations().translate(m_content->worldgen().biomes().at(cell.biome).translationKey, u"en"_s);
    }

    // Water, or the ice of a cold biome's water.
    static bool isWater(const QString &block) { return block.endsWith(u":water"_s) || block.endsWith(u"ice"_s); }
    // The ground of the terrain (not a tree, a plant or water standing on it).
    static bool isGround(const QcBlockDefinition &block)
    {
        return block.solid && !block.liquid && !block.name.endsWith(u"_log"_s) && !block.name.endsWith(u"_leaves"_s) &&
               !block.name.endsWith(u"cactus"_s);
    }
    // How far above and below the map's height the test looks for the top of the ground.
    static constexpr int groundSlack = 12;
    // The fewest cells of a kind whose share of matching heights is checked.
    static constexpr int minCompared = 50;

    // How many of the compared cells agree.
    struct Agreement
    {
        int agreeing = 0;
        int cells = 0;

        void add(bool agrees)
        {
            agreeing += agrees ? 1 : 0;
            ++cells;
        }
        [[nodiscard]] bool atLeast(int percent) const { return agreeing * 100 >= cells * percent; }
        [[nodiscard]] int misses() const { return cells - agreeing; }
        [[nodiscard]] QString text() const { return u"%1 of %2"_s.arg(agreeing).arg(cells); }
    };

    QTemporaryDir m_data;
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
};

QTEST_GUILESS_MAIN(TestWorldMapPreview)
#include "test_world_map_preview.moc"
