#include "clientcore/light/QcBlockLightEngine.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/parser/QcContentParser.h"
#include "common/core/QcSectionKey.h"
#include "common/world/QcLightVolume.h"

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Stairs and slabs of a light-blocking material: their states let light through except on the
// faces their boxes cover (decision E-104).
void addPartialShapes(QcContentRegistry &content)
{
    for (const char *shape : {
             R"({"name":"stairs","rotate":"facing","flip":"half","boxes":[[0,0,0,1,0.5,1],[0,0.5,0,1,1,0.5]]})",
             R"({"name":"slab","parts":[{"when":{"type":"bottom"},"boxes":[[0,0,0,1,0.5,1]]},
                 {"when":{"type":"top"},"boxes":[[0,0.5,0,1,1,1]]},{"when":{"type":"double"},"boxes":[[0,0,0,1,1,1]]}]})"}) {
        if (!content.registerShape(*QcContentParser::parseShape(QJsonDocument::fromJson(shape).object(), u"t"_s))) {
            qFatal("cannot register shape %s", shape);
        }
    }

    for (const char *block : {
             R"({"name":"stairs","shape":"stairs","states":{"facing":["north","east","south","west"],"half":["bottom","top"]},"textures":{"all":"stone"}})",
             R"({"name":"slab","shape":"slab","states":{"type":["bottom","top","double"]},"textures":{"all":"stone"}})"}) {
        const auto parsed = QcContentParser::parseBlock(QJsonDocument::fromJson(block).object(), u"t"_s);
        if (!parsed || !content.registerBlock(*parsed)) {
            qFatal("cannot register %s", block);
        }
    }
}

// Stone, a torch (14), a glowing opaque lamp (15), a dim block (opacity 3) and a weak glow (6),
// then stairs and slabs.
std::shared_ptr<QcContentRegistry> makeContent()
{
    auto content = std::make_shared<QcContentRegistry>();
    const auto add = [&content](const char *name, bool opaque, int opacity, int light) {
        QcBlockDefinition definition;
        definition.name = QString::fromLatin1(name);
        definition.opaque = opaque;
        definition.solid = opaque;
        definition.renderLayer = opaque ? QcRenderLayer::Opaque : QcRenderLayer::Cutout;
        definition.lightOpacity = opacity;
        definition.lightLevel = light;
        definition.faceTextures.fill(definition.name);
        if (!content->registerBlock(definition)) {
            qFatal("cannot register %s", name);
        }
    };

    add("t:stone", true, 15, 0);
    add("t:torch", false, 0, 14);
    add("t:lamp", true, 15, 15);
    add("t:leaves", false, 3, 0);
    add("t:glow", false, 1, 6);
    addPartialShapes(*content);

    return content;
}

constexpr int span = 4;
constexpr int first = -2;

// A 4 x 4 square of chunks (x and z from -2 to 1) with blocks of every kind near the bottom, the top
// and the middle of the world, and helpers to compare the engine with one full flood fill.
class Fixture
{
public:
    std::shared_ptr<QcContentRegistry> content = makeContent();
    std::shared_ptr<const QcBlockRenderTable> table =
        std::make_shared<const QcBlockRenderTable>(*content, QcTextureAtlas::build(*content));
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks;
    QSet<QcChunkPos> loaded;
    QRandomGenerator random{1234};
    // The square of chunks that is loaded and compared: all of them, or a smaller one for tests
    // that need less (cells outside it are dark walls for both).
    int checkFirst = first;
    int checkSpan = span;

    QcBlockId randomBlock()
    {
        const quint32 roll = random.bounded(100u);
        if (roll < 40) {
            return qcAirBlockId;
        }
        if (roll < 75) {
            return 1;
        }
        if (roll < 85) {
            return 4;
        }
        if (roll < 93) {
            return 2;
        }
        return roll < 97 ? 5 : 3;
    }

    void generate()
    {
        for (int cx = first; cx < first + span; ++cx) {
            for (int cz = first; cz < first + span; ++cz) {
                auto chunk = std::make_shared<QcChunk>(QcChunkPos{cx, cz});
                for (int y : {0, 1, 2, 3, 15, 16, 17, 64, 65, 66, 238, 239, 240, 254, 255}) {
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            if (random.bounded(100u) < 35) {
                                chunk->setBlock(x, y, z, randomBlock());
                            }
                        }
                    }
                }
                chunks.insert({cx, cz}, chunk);
            }
        }
    }

    QcBlockId state(const QString &name) const { return *content->stateId(name); }

    // Empty chunks of the checked square, all loaded into `engine`.
    void loadEmpty(QcBlockLightEngine &engine)
    {
        for (int cx = checkFirst; cx < checkFirst + checkSpan; ++cx) {
            for (int cz = checkFirst; cz < checkFirst + checkSpan; ++cz) {
                auto chunk = std::make_shared<QcChunk>(QcChunkPos{cx, cz});
                chunks.insert({cx, cz}, chunk);
                engine.addChunk(chunk);
                loaded.insert({cx, cz});
            }
        }
        engine.takeChanges();
    }

    // Sets one block in the fixture and the engine, as the client does after a block update.
    void set(QcBlockLightEngine &engine, QcBlockPos pos, QcBlockId block)
    {
        auto changed = std::make_shared<QcChunk>(*chunks.value(pos.chunk()));
        changed->setBlock(pos.localX(), pos.y, pos.localZ(), block);
        chunks.insert(pos.chunk(), changed);
        engine.changeBlock(pos, changed);
        engine.takeChanges();
    }

    // Full flood fill of the loaded chunks of the checked square; unloaded ones are dark walls.
    QcLightVolume reference() const
    {
        QcLightVolume volume(checkSpan * 16, QcWorld::worldHeight, checkSpan * 16);
        for (int cx = 0; cx < checkSpan; ++cx) {
            for (int cz = 0; cz < checkSpan; ++cz) {
                const QcChunkPos pos{checkFirst + cx, checkFirst + cz};
                const QcChunk *chunk = loaded.contains(pos) ? chunks.value(pos).get() : nullptr;
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            const int vx = cx * 16 + x;
                            const int vz = cz * 16 + z;
                            if (!chunk) {
                                volume.setOpacity(vx, y, vz, 15);
                                continue;
                            }
                            const QcBlockRenderTable::Entry &entry = table->entry(chunk->block(x, y, z));
                            volume.setOpacity(vx, y, vz, entry.lightOpacity, entry.lightBlockedFaces);
                            volume.addSource(vx, y, vz, entry.lightLevel);
                        }
                    }
                }
            }
        }

        volume.propagate();
        return volume;
    }

    // First difference between the engine (and its published snapshots) and the full flood fill.
    QString difference(const QcBlockLightEngine &engine) const
    {
        const QcLightVolume expected = reference();
        for (int vx = 0; vx < checkSpan * 16; ++vx) {
            for (int vz = 0; vz < checkSpan * 16; ++vz) {
                const QcBlockPos column{checkFirst * 16 + vx, 0, checkFirst * 16 + vz};
                const QcChunkLight published = engine.snapshot(column.chunk());
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    const QcBlockPos pos{column.x, y, column.z};
                    const int want = expected.level(vx, y, vz);
                    const int got = engine.level(pos);
                    const int shown = published.level(pos.localX(), y, pos.localZ());
                    if (want != got || want != shown) {
                        return u"at %1 %2 %3: engine %4, published %5, flood fill %6"_s.arg(pos.x).arg(y).arg(pos.z).arg(got).arg(shown).arg(want);
                    }
                }
            }
        }

        return {};
    }
};

// Sets one random block, mostly on chunk borders and in the bottom and top sections of the world,
// in the fixture and the engine, and returns the engine's changes.
QcBlockLightEngine::Changes editRandomBlock(Fixture &fixture, QcBlockLightEngine &engine)
{
    QRandomGenerator &random = fixture.random;
    const auto coordinate = [&random] {
        const int chunk = first + int(random.bounded(quint32(span)));
        const quint32 roll = random.bounded(4u);
        const int local = roll == 0 ? 0 : (roll == 1 ? 15 : int(random.bounded(16u)));
        return chunk * 16 + local;
    };
    const quint32 band = random.bounded(4u);
    const int y = band == 0 ? int(random.bounded(18u)) : (band == 1 ? 238 + int(random.bounded(18u)) : 60 + int(random.bounded(10u)));
    const QcBlockPos pos{coordinate(), y, coordinate()};

    auto changed = std::make_shared<QcChunk>(*fixture.chunks.value(pos.chunk()));
    changed->setBlock(pos.localX(), pos.y, pos.localZ(), fixture.randomBlock());
    fixture.chunks.insert(pos.chunk(), changed);
    engine.changeBlock(pos, changed);
    return engine.takeChanges();
}

QSet<quint64> remeshedSections(const QcBlockLightEngine::Changes &changes)
{
    QSet<quint64> remesh;
    for (const QcBlockLightEngine::SectionRef &ref : changes.remesh) {
        remesh.insert(qcSectionKey(ref.chunk, ref.section));
    }
    return remesh;
}

// The first block around `cell` (itself included) inside the world whose section is not remeshed.
QString unmeshedReader(QcBlockPos cell, const QSet<quint64> &remesh)
{
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                const QcBlockPos reader = cell.offset(dx, dy, dz);
                if (reader.isInsideWorldHeight() && !remesh.contains(qcSectionKey(reader.chunk(), reader.y / 16))) {
                    return u"cell %1 %2 %3 changed, section of %4 %5 %6 not remeshed"_s.arg(cell.x).arg(cell.y).arg(cell.z).arg(reader.x).arg(reader.y).arg(reader.z);
                }
            }
        }
    }
    return {};
}

// Counts the cells of `chunk` whose light differs between `was` and `now` into `lightChanges` and
// returns the first one read by a section that is not remeshed.
QString unmeshedLightChange(QcChunkPos chunk, const QcChunkLight &now, const QcChunkLight &was, const QSet<quint64> &remesh, int &lightChanges)
{
    for (int section = 0; section < QcWorld::sectionCount; ++section) {
        if (now.sections[std::size_t(section)] == was.sections[std::size_t(section)]) {
            continue;
        }
        for (int ly = 0; ly < 16; ++ly) {
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx) {
                    const int cellY = section * 16 + ly;
                    if (now.level(lx, cellY, lz) == was.level(lx, cellY, lz)) {
                        continue;
                    }

                    ++lightChanges;
                    const QString problem = unmeshedReader({chunk.x * 16 + lx, cellY, chunk.z * 16 + lz}, remesh);
                    if (!problem.isEmpty()) {
                        return problem;
                    }
                }
            }
        }
    }
    return {};
}

// Unloads a random chunk and loads it again; returns the first difference to the flood fill while it
// is away.
QString reloadRandomChunk(Fixture &fixture, QcBlockLightEngine &engine)
{
    QRandomGenerator &random = fixture.random;
    const QcChunkPos away{first + int(random.bounded(quint32(span))), first + int(random.bounded(quint32(span)))};
    engine.removeChunk(away);
    fixture.loaded.remove(away);
    engine.takeChanges();
    QString problem = fixture.difference(engine);
    if (!problem.isEmpty()) {
        return problem;
    }

    engine.addChunk(fixture.chunks.value(away));
    fixture.loaded.insert(away);
    engine.takeChanges();
    return {};
}

} // namespace

class TestBlockLightEngine : public QObject
{
    Q_OBJECT

private slots:
    void loadingMatchesTheFullFloodFill()
    {
        Fixture fixture;
        fixture.generate();
        QcBlockLightEngine engine(fixture.table);

        // In a scattered order, so light flows into chunks that are already there and out of new ones.
        const QList<QcChunkPos> order{{0, 0}, {-2, 1}, {1, -2}, {-1, 0}, {0, -1}, {-1, -1}, {1, 1}, {-2, -2},
                                      {0, 1}, {1, 0}, {-1, 1}, {1, -1}, {-2, 0}, {0, -2}, {-2, -1}, {-1, -2}};
        for (const QcChunkPos &pos : order) {
            engine.addChunk(fixture.chunks.value(pos));
            fixture.loaded.insert(pos);
            engine.takeChanges();
            if (fixture.loaded.size() % 5 == 0) {
                const QString problem = fixture.difference(engine);
                QVERIFY2(problem.isEmpty(), qPrintable(problem));
            }
        }

        const QString problem = fixture.difference(engine);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }

    void randomEditsMatchTheFullFloodFill()
    {
        Fixture fixture;
        fixture.generate();
        QcBlockLightEngine engine(fixture.table);
        for (auto it = fixture.chunks.constBegin(); it != fixture.chunks.constEnd(); ++it) {
            engine.addChunk(it.value());
            fixture.loaded.insert(it.key());
        }
        engine.takeChanges();

        QHash<QcChunkPos, QcChunkLight> before;
        const auto remember = [&] {
            for (const QcChunkPos &pos : std::as_const(fixture.loaded)) {
                before.insert(pos, engine.snapshot(pos));
            }
        };
        remember();

        int lightChanges = 0;
        for (int edit = 1; edit <= 400; ++edit) {
            const QcBlockLightEngine::Changes changes = editRandomBlock(fixture, engine);
            // Every changed cell is read by the sections the scheduler meshes again: its own and,
            // for border cells, the neighbouring ones.
            const QSet<quint64> remesh = remeshedSections(changes);
            for (const QcChunkPos &chunk : std::as_const(fixture.loaded)) {
                const QcChunkLight now = engine.snapshot(chunk);
                const QcChunkLight &was = before[chunk];
                const QString unmeshed = unmeshedLightChange(chunk, now, was, remesh, lightChanges);
                QVERIFY2(unmeshed.isEmpty(), qPrintable(unmeshed));
            }

            if (edit % 50 == 25) {
                // Now and then a chunk goes away and comes back.
                const QString problem = reloadRandomChunk(fixture, engine);
                QVERIFY2(problem.isEmpty(), qPrintable(u"after unloading: "_s + problem));
            }
            remember();
            if (edit % 40 == 0) {
                const QString problem = fixture.difference(engine);
                QVERIFY2(problem.isEmpty(), qPrintable(u"after %1 edits: "_s.arg(edit) + problem));
            }
        }

        // The edits really moved light around.
        QVERIFY2(lightChanges > 1000, qPrintable(QString::number(lightChanges)));
    }

    void stairsAndSlabsMatchTheFullFloodFill()
    {
        // The four chunks around the origin are enough.
        Fixture fixture;
        fixture.checkFirst = -1;
        fixture.checkSpan = 2;
        QcBlockLightEngine engine(fixture.table);
        fixture.loadEmpty(engine);

        // A closed stone tube two blocks high running south across the chunk border at z = 0 (its
        // west wall lies across the border at x = 0), a torch at its north end and a bottom stair in
        // the middle; light reaches the far end only through or over the stair.
        constexpr int y = 64;
        for (int z = -3; z <= 4; ++z) {
            for (int dy = -1; dy <= 2; ++dy) {
                for (int x = -1; x <= 1; ++x) {
                    const bool inside = x == 0 && dy >= 0 && dy <= 1 && z >= -2 && z <= 3;
                    fixture.set(engine, {x, y + dy, z}, inside ? qcAirBlockId : QcBlockId(1));
                }
            }
        }
        const QcBlockPos stair{0, y, 0};
        const QcBlockPos beyond{0, y, 2};
        fixture.set(engine, {0, y, -2}, 2);
        const auto expectMatch = [&fixture, &engine](const char *step) {
            const QString problem = fixture.difference(engine);
            QVERIFY2(problem.isEmpty(), qPrintable(QString::fromLatin1(step) + u": "_s + problem));
        };
        expectMatch("tube");

        // Facing north the stair covers its north face (towards the torch) and its bottom; light
        // comes in from above only.
        const QcBlockId north = fixture.state(u"t:stairs[facing=north,half=bottom]"_s);
        const QcBlockId south = fixture.state(u"t:stairs[facing=south,half=bottom]"_s);
        QCOMPARE(fixture.table->entry(north).lightOpacity, fixture.table->entry(south).lightOpacity);
        QVERIFY(fixture.table->entry(north).lightBlockedFaces != fixture.table->entry(south).lightBlockedFaces);
        fixture.set(engine, stair, north);
        expectMatch("stair facing north");
        const int northLevel = engine.level(beyond);
        const int litFromAbove = engine.level(stair);

        // Turned around: the same opacity, a different covered face; light now enters the stair
        // straight from the torch and is stopped on its south face.
        fixture.set(engine, stair, south);
        expectMatch("stair facing south");
        QVERIFY2(engine.level(stair) > litFromAbove, qPrintable(u"%1 %2"_s.arg(engine.level(stair)).arg(litFromAbove)));
        fixture.set(engine, stair, north);
        expectMatch("stair facing north again");
        QCOMPARE(engine.level(beyond), northLevel);
        fixture.set(engine, stair, fixture.state(u"t:stairs[facing=east,half=top]"_s));
        expectMatch("upside-down stair facing east");

        // Slabs above the stair close the way over it, from below and from above, then go away.
        const QcBlockPos over = stair.offset(0, 1, 0);
        for (const char *slab : {"t:slab[type=bottom]", "t:slab[type=top]", "t:slab[type=double]"}) {
            fixture.set(engine, over, fixture.state(QString::fromLatin1(slab)));
            expectMatch(slab);
        }
        fixture.set(engine, over, qcAirBlockId);
        expectMatch("slab removed");
        fixture.set(engine, stair, qcAirBlockId);
        expectMatch("stair removed");
    }
};

QTEST_GUILESS_MAIN(TestBlockLightEngine)
#include "test_block_light_engine.moc"
