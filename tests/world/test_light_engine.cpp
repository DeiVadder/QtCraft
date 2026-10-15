#include "QcContentParser.h"
#include "QcContentRegistry.h"
#include "QcLightEngine.h"
#include "QcLightVolume.h"

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using Channel = QcLightEngine::Channel;

namespace {

void addPartialShapes(QcContentRegistry &content)
{
    for (const char *shape : {
             R"({"name":"slab","parts":[{"when":{"type":"bottom"},"boxes":[[0,0,0,1,0.5,1]]},
                 {"when":{"type":"top"},"boxes":[[0,0.5,0,1,1,1]]},{"when":{"type":"double"},"boxes":[[0,0,0,1,1,1]]}]})",
             R"({"name":"stairs","rotate":"facing","flip":"half","boxes":[[0,0,0,1,0.5,1],[0,0.5,0,1,1,0.5]]})"}) {
        if (!content.registerShape(*QcContentParser::parseShape(QJsonDocument::fromJson(shape).object(), u"t"_s))) {
            qFatal("cannot register shape %s", shape);
        }
    }
    for (const char *block : {
             R"({"name":"slab","shape":"slab","states":{"type":["bottom","top","double"]},"textures":{"all":"stone"}})",
             R"({"name":"stairs","shape":"stairs","states":{"facing":["north","east","south","west"],"half":["bottom","top"]},"textures":{"all":"stone"}})"}) {
        const auto parsed = QcContentParser::parseBlock(QJsonDocument::fromJson(block).object(), u"t"_s);
        if (!parsed || !content.registerBlock(*parsed)) {
            qFatal("cannot register %s", block);
        }
    }
}

// Ids: 1 stone, 2 torch (14), 3 lamp (opaque, 15), 4 leaves (opacity 3), 5 glow (opacity 1, 6),
// 6 water (opacity 2), 7 glass (opacity 0), then slabs and stairs.
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
    add("t:water", false, 2, 0);
    add("t:glass", false, 0, 0);
    addPartialShapes(*content);
    return content;
}

constexpr int span = 3;
constexpr int first = -1;
constexpr QcBlockId stone = 1;

// A 3 x 3 square of chunks (x and z from -1 to 1) with a rough ground, overhangs, canopies and
// random blocks, an engine on it and the full flood fill both channels must equal.
class Fixture
{
public:
    std::shared_ptr<QcContentRegistry> content = makeContent();
    std::shared_ptr<const QcLightTable> table = std::make_shared<const QcLightTable>(*content);
    QcLightEngine engine{table, {}};
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
    QSet<QcChunkPos> loaded;
    QRandomGenerator random{4711};
    QList<QcBlockId> palette;

    Fixture()
    {
        palette = {qcAirBlockId, stone, 2, 3, 4, 5, 6, 7};
        for (const QString &name : {u"t:slab[type=bottom]"_s, u"t:slab[type=top]"_s, u"t:stairs[facing=north,half=bottom]"_s,
                                   u"t:stairs[facing=east,half=top]"_s}) {
            palette.append(*content->stateId(name));
        }
    }

    QcBlockId randomBlock() { return palette[qsizetype(random.bounded(quint32(palette.size())))]; }

    // A rough ground with random blocks above it, on overhang and canopy heights and at the top.
    void generateColumn(QcChunk &chunk, int x, int z)
    {
        const int ground = 60 + int(random.bounded(6u));
        for (int y = 0; y <= ground; ++y) {
            chunk.setBlock(x, y, z, stone);
        }
        for (int y : {ground + 1, ground + 2, 70, 71, 72, 90, 254, 255}) {
            if (random.bounded(100u) < 30) {
                chunk.setBlock(x, y, z, randomBlock());
            }
        }
    }

    void generate()
    {
        for (int cx = first; cx < first + span; ++cx) {
            for (int cz = first; cz < first + span; ++cz) {
                auto chunk = std::make_shared<QcChunk>(QcChunkPos{cx, cz});
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        generateColumn(*chunk, x, z);
                    }
                }
                chunks.insert({cx, cz}, chunk);
            }
        }
    }

    // Chunks of the square made by `column` (called per column) and loaded.
    template<typename Column>
    void fill(Column &&column)
    {
        for (int cx = first; cx < first + span; ++cx) {
            for (int cz = first; cz < first + span; ++cz) {
                auto chunk = std::make_shared<QcChunk>(QcChunkPos{cx, cz});
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        column(*chunk, x, z);
                    }
                }
                chunks.insert({cx, cz}, chunk);
            }
        }
        loadAll();
    }

    void loadAll()
    {
        for (auto it = chunks.cbegin(); it != chunks.cend(); ++it) {
            engine.addChunk(*it.value());
            loaded.insert(it.key());
        }
    }

    // Sets one block as the server does: in place, then tells the engine.
    void set(QcBlockPos pos, QcBlockId block)
    {
        QcChunk &chunk = *chunks.value(pos.chunk());
        const QcBlockId before = chunk.block(pos.localX(), pos.y, pos.localZ());
        chunk.setBlock(pos.localX(), pos.y, pos.localZ(), block);
        engine.changeBlock(pos, before, chunk);
    }

    // Highest cell of a column of `chunk` that sky light straight down does not reach.
    int columnTop(const QcChunk &chunk, int x, int z) const
    {
        for (int y = QcWorld::worldHeight - 1; y >= 0; --y) {
            const QcBlockState &state = content->state(chunk.block(x, y, z));
            const auto stop = QcLightPassage::skyStop(state.lightOpacity, state.lightBlockedFaces);
            if (stop != QcLightPassage::SkyStop::None) {
                return QcLightPassage::skyTop(stop, y);
            }
        }
        return -1;
    }

    // Full flood fill of the loaded chunks of the square for one channel; unloaded ones are walls.
    QcLightVolume reference(Channel channel) const
    {
        QcLightVolume volume(span * 16, QcWorld::worldHeight, span * 16);
        for (int cx = 0; cx < span; ++cx) {
            for (int cz = 0; cz < span; ++cz) {
                const QcChunkPos pos{first + cx, first + cz};
                const QcChunk *chunk = loaded.contains(pos) ? chunks.value(pos).get() : nullptr;
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        fillColumn(volume, channel, chunk, cx * 16 + x, cz * 16 + z);
                    }
                }
            }
        }
        volume.propagate();
        return volume;
    }

    void fillColumn(QcLightVolume &volume, Channel channel, const QcChunk *chunk, int vx, int vz) const
    {
        const int x = vx % 16;
        const int z = vz % 16;
        const int top = chunk ? columnTop(*chunk, x, z) : QcWorld::worldHeight;
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            if (!chunk) {
                volume.setOpacity(vx, y, vz, 15);
                continue;
            }
            const QcLightTable::Entry &entry = table->entry(chunk->block(x, y, z));
            volume.setOpacity(vx, y, vz, entry.opacity, entry.blockedFaces);
            const int source = channel == Channel::Block ? entry.emission : (y > top ? 15 : 0);
            if (source > 0) {
                volume.addSource(vx, y, vz, source);
            }
        }
    }

    // First difference between the engine and the full flood fill of a channel.
    QString difference(Channel channel) const
    {
        const QcLightVolume expected = reference(channel);
        for (int vx = 0; vx < span * 16; ++vx) {
            for (int vz = 0; vz < span * 16; ++vz) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    const QcBlockPos pos{first * 16 + vx, y, first * 16 + vz};
                    const int want = expected.level(vx, y, vz);
                    const int got = engine.level(channel, pos);
                    if (want != got) {
                        return u"%1 light at %2 %3 %4: engine %5, flood fill %6"_s.arg(channel == Channel::Sky ? u"sky"_s : u"block"_s)
                            .arg(pos.x).arg(y).arg(pos.z).arg(got).arg(want);
                    }
                }
            }
        }
        return {};
    }

    QString differences() const
    {
        const QString block = difference(Channel::Block);
        return block.isEmpty() ? difference(Channel::Sky) : block;
    }

    QcBlockPos randomPos()
    {
        const auto coordinate = [this] {
            const int chunk = first + int(random.bounded(quint32(span)));
            const quint32 roll = random.bounded(4u);
            const int local = roll == 0 ? 0 : (roll == 1 ? 15 : int(random.bounded(16u)));
            return chunk * 16 + local;
        };
        const quint32 band = random.bounded(4u);
        const int y = band == 0 ? 55 + int(random.bounded(20u)) : (band == 1 ? 236 + int(random.bounded(20u)) : int(random.bounded(256u)));
        return {coordinate(), y, coordinate()};
    }
};

} // namespace

class TestLightEngine : public QObject
{
    Q_OBJECT

private slots:
    void loadingMatchesTheFullFloodFill()
    {
        Fixture fixture;
        fixture.generate();
        fixture.loadAll();
        QCOMPARE(fixture.differences(), QString());
    }

    // Incremental updates equal one full recompute, whatever the edits (deterministic seed).
    void randomEditsMatchTheFullFloodFill()
    {
        Fixture fixture;
        fixture.generate();
        fixture.loadAll();
        for (int round = 0; round < 8; ++round) {
            for (int edit = 0; edit < 40; ++edit) {
                fixture.set(fixture.randomPos(), fixture.randomBlock());
            }
            QCOMPARE(fixture.differences(), QString());
        }
    }

    // Chunks whose own light was prepared on their own (in the background on the server) join their
    // neighbours with the same result, in any order, also when they replace a loaded chunk.
    void preparedChunksMatchTheFullFloodFill()
    {
        Fixture fixture;
        fixture.generate();
        QList<QcChunkPos> order = fixture.chunks.keys();
        std::ranges::sort(order, [](QcChunkPos a, QcChunkPos b) { return a.x * 7 + a.z * 3 < b.x * 7 + b.z * 3; });
        for (const QcChunkPos pos : std::as_const(order)) {
            const QcChunk &chunk = *fixture.chunks.value(pos);
            fixture.engine.addChunk(chunk, fixture.engine.prepare(chunk));
            fixture.loaded.insert(pos);
        }
        QCOMPARE(fixture.differences(), QString());

        for (int edit = 0; edit < 40; ++edit) {
            fixture.set(fixture.randomPos(), fixture.randomBlock());
        }
        const QcChunk &middle = *fixture.chunks.value({0, 0});
        fixture.engine.addChunk(middle, fixture.engine.prepare(middle));
        QCOMPARE(fixture.differences(), QString());
    }

    // Chunks leave and come back in any order; the light follows across their borders.
    void unloadAndReloadMatchTheFullFloodFill()
    {
        Fixture fixture;
        fixture.generate();
        fixture.loadAll();
        for (int round = 0; round < 6; ++round) {
            const QcChunkPos gone{first + int(fixture.random.bounded(quint32(span))), first + int(fixture.random.bounded(quint32(span)))};
            fixture.engine.removeChunk(gone);
            fixture.loaded.remove(gone);
            for (int edit = 0; edit < 10; ++edit) {
                const QcBlockPos pos = fixture.randomPos();
                if (fixture.loaded.contains(pos.chunk())) {
                    fixture.set(pos, fixture.randomBlock());
                }
            }
            QCOMPARE(fixture.differences(), QString());

            fixture.engine.addChunk(*fixture.chunks.value(gone));
            fixture.loaded.insert(gone);
            QCOMPARE(fixture.differences(), QString());
        }
    }

    // Prepared chunks (the server's background path) mixed with removals and edits in any order
    // give the light of one full flood fill.
    void preparedAddsAndRemovalsMatchTheFullFloodFill()
    {
        Fixture fixture;
        fixture.generate();
        for (auto it = fixture.chunks.cbegin(); it != fixture.chunks.cend(); ++it) {
            fixture.engine.addChunk(*it.value(), fixture.engine.prepare(*it.value()));
            fixture.loaded.insert(it.key());
        }
        for (int round = 0; round < 6; ++round) {
            const QcChunkPos gone{first + int(fixture.random.bounded(quint32(span))), first + int(fixture.random.bounded(quint32(span)))};
            fixture.engine.removeChunk(gone);
            fixture.loaded.remove(gone);
            for (int edit = 0; edit < 10; ++edit) {
                const QcBlockPos pos = fixture.randomPos();
                if (fixture.loaded.contains(pos.chunk())) {
                    fixture.set(pos, fixture.randomBlock());
                }
            }
            QCOMPARE(fixture.differences(), QString());

            // Prepared while it was away, as the server's generator or I/O thread does.
            const QcChunk &back = *fixture.chunks.value(gone);
            const std::shared_ptr<QcLightEngine::Prepared> prepared = fixture.engine.prepare(back);
            fixture.engine.addChunk(back, prepared);
            fixture.loaded.insert(gone);
            QCOMPARE(fixture.differences(), QString());
        }
    }

    // A roof taken away opens the sections under it to the sky; once they are all lit with 15 they
    // keep no cells again.
    void sectionsOpenedToTheSkyLoseTheirCells()
    {
        Fixture fixture;
        fixture.fill([](QcChunk &chunk, int x, int z) {
            chunk.setBlock(x, 60, z, stone);
            if (chunk.pos() == QcChunkPos{0, 0}) {
                chunk.setBlock(x, 100, z, stone);
            }
        });
        // Under the roof the sky light comes in from the sides, so those sections hold cells.
        QVERIFY(fixture.engine.section(Channel::Sky, {0, 0}, 5));
        QVERIFY(fixture.engine.memory().sections[1] > 9);

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                fixture.set({x, 100, z}, qcAirBlockId);
            }
        }
        QCOMPARE(fixture.differences(), QString());
        QCOMPARE(fixture.engine.section(Channel::Sky, {0, 0}, 5), nullptr);
        QCOMPARE(fixture.engine.uniformLevel(Channel::Sky, {0, 0}, 5), 15);
        // As without the roof: only the sections of the ground are partly lit.
        QCOMPARE(fixture.engine.memory().sections[1], 9);
    }

    // Sky light under an overhang comes in from the side; a roof keeps it out entirely.
    void skyLightUnderAnOverhang()
    {
        Fixture fixture;
        fixture.fill([](QcChunk &chunk, int x, int z) { chunk.setBlock(x, 60, z, stone); });
        QCOMPARE(fixture.engine.skyLight({3, 61, 3}), 15);

        // A roof over x 0..15 of chunk 0 at y 64: the cells below light from the open side x < 0.
        for (int x = 0; x < 16; ++x) {
            for (int z = -16; z < 32; ++z) {
                fixture.set({x, 64, z}, stone);
            }
        }
        QCOMPARE(fixture.engine.skyLight({-1, 61, 5}), 15);
        QCOMPARE(fixture.engine.skyLight({0, 61, 5}), 14);
        QCOMPARE(fixture.engine.skyLight({5, 61, 5}), 9);
        QCOMPARE(fixture.engine.skyLight({14, 62, 5}), 13);
        QCOMPARE(fixture.engine.skyLight({16, 61, 5}), 15);
        QCOMPARE(fixture.engine.skyLight({15, 61, 5}), 14);
        QCOMPARE(fixture.engine.skyLight({7, 61, 5}), 7);
        QCOMPARE(fixture.engine.skyLight({8, 61, 5}), 7);
        QCOMPARE(fixture.differences(), QString());

        // Walls close the open sides: dark under the roof.
        for (int y = 61; y < 64; ++y) {
            for (int z = -16; z < 32; ++z) {
                fixture.set({-1, y, z}, stone);
                fixture.set({16, y, z}, stone);
            }
        }
        QCOMPARE(fixture.engine.skyLight({5, 61, 5}), 0);
        QCOMPARE(fixture.differences(), QString());
    }

    // Water and leaves dim sky light by their opacity per step; glass and air do not.
    void skyLightThroughWaterLeavesAndGlass()
    {
        Fixture fixture;
        fixture.fill([](QcChunk &chunk, int x, int z) {
            chunk.setBlock(x, 40, z, stone);
            for (int y = 41; y <= 50; ++y) {
                chunk.setBlock(x, y, z, 6);
            }
            chunk.setBlock(x, 100, z, 7);
        });
        QCOMPARE(fixture.engine.skyLight({4, 99, 4}), 15);
        QCOMPARE(fixture.engine.skyLight({4, 51, 4}), 15);
        QCOMPARE(fixture.engine.skyLight({4, 50, 4}), 13);
        QCOMPARE(fixture.engine.skyLight({4, 49, 4}), 11);
        QCOMPARE(fixture.engine.skyLight({4, 44, 4}), 1);
        QCOMPARE(fixture.engine.skyLight({4, 43, 4}), 0);

        Fixture canopy;
        canopy.fill([](QcChunk &chunk, int x, int z) {
            chunk.setBlock(x, 60, z, stone);
            chunk.setBlock(x, 80, z, 4);
            chunk.setBlock(x, 81, z, 4);
        });
        QCOMPARE(canopy.engine.skyTop({4, 0, 4}), 81);
        QCOMPARE(canopy.engine.skyLight({4, 81, 4}), 12);
        QCOMPARE(canopy.engine.skyLight({4, 80, 4}), 9);
        QCOMPARE(canopy.engine.skyLight({4, 79, 4}), 8);
        QCOMPARE(canopy.engine.skyLight({4, 72, 4}), 1);
        QCOMPARE(canopy.differences(), QString());
    }

    // A bottom slab is lit by the sky but keeps it from the cell below; a top slab keeps it out of
    // its own cell (decision E-104).
    void skyLightAtSlabs()
    {
        Fixture fixture;
        fixture.fill([](QcChunk &chunk, int x, int z) { chunk.setBlock(x, 60, z, stone); });
        // A shaft one cell wide at (2, 2) from 61 to 65.
        for (int x = 1; x <= 3; ++x) {
            for (int z = 1; z <= 3; ++z) {
                for (int y = 61; y <= 65; ++y) {
                    if (x != 2 || z != 2) {
                        fixture.set({x, y, z}, stone);
                    }
                }
            }
        }
        QCOMPARE(fixture.engine.skyLight({2, 61, 2}), 15);

        fixture.set({2, 62, 2}, *fixture.content->stateId(u"t:slab[type=bottom]"_s));
        QCOMPARE(fixture.engine.skyTop({2, 0, 2}), 61);
        QCOMPARE(fixture.engine.skyLight({2, 62, 2}), 15);
        QCOMPARE(fixture.engine.skyLight({2, 61, 2}), 0);
        QCOMPARE(fixture.differences(), QString());

        fixture.set({2, 62, 2}, *fixture.content->stateId(u"t:slab[type=top]"_s));
        QCOMPARE(fixture.engine.skyTop({2, 0, 2}), 62);
        QCOMPARE(fixture.engine.skyLight({2, 62, 2}), 0);
        QCOMPARE(fixture.engine.skyLight({2, 63, 2}), 15);
        QCOMPARE(fixture.differences(), QString());

        fixture.set({2, 62, 2}, qcAirBlockId);
        QCOMPARE(fixture.engine.skyTop({2, 0, 2}), 60);
        QCOMPARE(fixture.engine.skyLight({2, 61, 2}), 15);
        QCOMPARE(fixture.differences(), QString());
    }

    // Light sources of every level, in partial shapes and next to them.
    void blockLightSources()
    {
        Fixture fixture;
        fixture.chunks.insert({0, 0}, std::make_shared<QcChunk>(QcChunkPos{0, 0}));
        fixture.engine.addChunk(*fixture.chunks.value({0, 0}));
        fixture.loaded.insert({0, 0});
        fixture.set({8, 100, 8}, 2);
        QCOMPARE(fixture.engine.blockLight({8, 100, 8}), 14);
        QCOMPARE(fixture.engine.blockLight({8, 100, 11}), 11);
        fixture.set({8, 100, 9}, 3);
        QCOMPARE(fixture.engine.blockLight({8, 100, 9}), 15);
        QCOMPARE(fixture.engine.blockLight({8, 100, 10}), 14);
        fixture.set({8, 100, 9}, qcAirBlockId);
        QCOMPARE(fixture.engine.blockLight({8, 100, 10}), 12);
        fixture.set({8, 100, 8}, qcAirBlockId);
        QCOMPARE(fixture.engine.blockLight({8, 100, 10}), 0);
        QCOMPARE(fixture.engine.memory().sections[0], 0);
    }

    // Sections lit wholly by the open sky or wholly dark keep no cells.
    void uniformSectionsKeepNoCells()
    {
        Fixture fixture;
        fixture.fill([](QcChunk &chunk, int x, int z) { chunk.setBlock(x, 60, z, stone); });
        const QcLightEngine::Memory memory = fixture.engine.memory();
        QCOMPARE(memory.columns, 9);
        // Only the section of the ground (48..63) is partly lit.
        QCOMPARE(memory.sections[1], 9);
        QCOMPARE(memory.sections[0], 0);
        QCOMPARE(fixture.engine.section(Channel::Sky, {0, 0}, 10), nullptr);
        QCOMPARE(fixture.engine.uniformLevel(Channel::Sky, {0, 0}, 10), 15);
        QCOMPARE(fixture.engine.section(Channel::Sky, {0, 0}, 1), nullptr);
        QCOMPARE(fixture.engine.uniformLevel(Channel::Sky, {0, 0}, 1), 0);
    }
};

QTEST_GUILESS_MAIN(TestLightEngine)
#include "test_light_engine.moc"
