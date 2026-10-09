#include "QcBlockLightEngine.h"
#include "QcLightVolume.h"
#include "QcSectionKey.h"
#include "QcTextureAtlas.h"

#include <QRandomGenerator>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Stone, a torch (14), a glowing opaque lamp (15), a dim block (opacity 3) and a weak glow (6).
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

    // Full flood fill of the loaded chunks; unloaded ones are dark walls.
    QcLightVolume reference() const
    {
        QcLightVolume volume(span * 16, QcWorld::worldHeight, span * 16);
        for (int cx = 0; cx < span; ++cx) {
            for (int cz = 0; cz < span; ++cz) {
                const QcChunkPos pos{first + cx, first + cz};
                const bool present = loaded.contains(pos);
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            const int vx = cx * 16 + x;
                            const int vz = cz * 16 + z;
                            if (!present) {
                                volume.setOpacity(vx, y, vz, 15);
                                continue;
                            }
                            const QcBlockRenderTable::Entry &entry = table->entry(chunks.value(pos)->block(x, y, z));
                            volume.setOpacity(vx, y, vz, entry.lightOpacity);
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
        for (int vx = 0; vx < span * 16; ++vx) {
            for (int vz = 0; vz < span * 16; ++vz) {
                const QcBlockPos column{first * 16 + vx, 0, first * 16 + vz};
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
};

QTEST_GUILESS_MAIN(TestBlockLightEngine)
#include "test_block_light_engine.moc"
