#include "QcBlockLightEngine.h"
#include "QcChunkMesher.h"
#include "QcContentRegistry.h"
#include "QcLightEngine.h"
#include "QcSectionLightPool.h"
#include "QcTextureAtlas.h"

#include <QRandomGenerator>
#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

// Stone, a torch (14) and leaves (opacity 3).
struct Fixture
{
    QcContentRegistry content;
    QcBlockId stone = 0;
    QcBlockId torch = 0;
    QcBlockId leaves = 0;
    std::shared_ptr<const QcBlockRenderTable> table;

    Fixture()
    {
        const auto add = [this](const QString &name, bool opaque, int opacity, int light) {
            QcBlockDefinition definition;
            definition.name = name;
            definition.opaque = opaque;
            definition.solid = opaque;
            definition.renderLayer = opaque ? QcRenderLayer::Opaque : QcRenderLayer::Cutout;
            definition.lightOpacity = opacity;
            definition.lightLevel = light;
            definition.faceTextures.fill(name);
            return *content.registerBlock(definition);
        };
        stone = add(u"t:stone"_s, true, 15, 0);
        torch = add(u"t:torch"_s, false, 0, 14);
        leaves = add(u"t:leaves"_s, false, 3, 0);
        table = std::make_shared<const QcBlockRenderTable>(content, QcTextureAtlas::build(content));
    }

    // A stone floor at y = 20 with holes, a roof at y = 27 over the western half, random stone,
    // leaves and torches between them.
    void fill(QcChunk &chunk, QRandomGenerator &random) const
    {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                if (random.bounded(10) != 0) {
                    chunk.setBlock(x, 20, z, stone);
                }
                if (x < 8) {
                    chunk.setBlock(x, 27, z, stone);
                }
                const int y = 21 + random.bounded(6);
                const quint32 roll = random.bounded(20u);
                chunk.setBlock(x, y, z, roll == 0 ? torch : (roll < 4 ? leaves : (roll < 6 ? stone : qcAirBlockId)));
            }
        }
    }

    // 3 x 3 chunks around (0, 0), each filled by fill().
    std::array<std::shared_ptr<const QcChunk>, 9> chunks() const
    {
        QRandomGenerator random(99);
        std::array<std::shared_ptr<const QcChunk>, 9> result;
        for (int i = 0; i < 9; ++i) {
            auto chunk = std::make_shared<QcChunk>(QcChunkPos{i % 3 - 1, i / 3 - 1});
            fill(*chunk, random);
            result[std::size_t(i)] = std::move(chunk);
        }
        return result;
    }
};

// What the GPU holds in the texels of one slot, read back slice by slice.
QByteArray readSlot(QRhi *rhi, QRhiTexture *page, int firstSlice)
{
    QByteArray texels;
    for (int y = 0; y < QcSectionLightPool::sectionSize; ++y) {
        QRhiCommandBuffer *commands = nullptr;
        if (rhi->beginOffscreenFrame(&commands) != QRhi::FrameOpSuccess) {
            return {};
        }
        QRhiReadbackResult result;
        QRhiReadbackDescription description(page);
        description.setLayer(firstSlice + y);
        QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
        updates->readBackTexture(description, &result);
        commands->resourceUpdate(updates);
        rhi->endOffscreenFrame();
        texels += result.data;
    }
    return texels;
}

} // namespace

// The per-cell light of the sections for later shaders (decision E-390): what the mesher hands
// over, the texel layout, and the GPU pool that holds it (uploaded and read back offscreen).
class TestSectionLight : public QObject
{
    Q_OBJECT

    std::unique_ptr<QRhi> m_rhi;

private slots:
    void initTestCase()
    {
#if QT_CONFIG(metal)
        QRhiMetalInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Metal, &params));
#endif
    }

    void uniformChannelsHoldNoCells()
    {
        auto dark = std::make_shared<QcLightNibbles>(0);
        auto lit = std::make_shared<QcLightNibbles>(15);
        const QcSectionLight uniform = QcSectionLight::fromChannels(dark, lit);
        QVERIFY(uniform.isUniform());
        QCOMPARE(uniform.uniformBlock, 0);
        QCOMPARE(uniform.uniformSky, 15);

        auto mixed = std::make_shared<QcLightNibbles>(15);
        mixed->setLevel(QcChunkSection::index(3, 4, 5), 7);
        const QcSectionLight light = QcSectionLight::fromChannels(nullptr, mixed);
        QVERIFY(!light.isUniform());
        QVERIFY(!light.block);
        QCOMPARE(light.skyLevel(QcChunkSection::index(3, 4, 5)), 7);

        // RG8 texels in cell order: x fastest, then z, then y.
        const QByteArray texels = light.texels();
        QCOMPARE(texels.size(), 4096 * 2);
        const qsizetype at = qsizetype(QcChunkSection::index(3, 4, 5)) * 2;
        QCOMPARE(quint8(texels[at]), quint8(0));
        QCOMPARE(quint8(texels[at + 1]), quint8(7 * 17));
        QCOMPARE(quint8(texels[1]), quint8(255));
        QCOMPARE(at, qsizetype((4 * 16 + 5) * 16 + 3) * 2);
    }

    // The mesher hands over the light of the section's own cells: block light like the client's
    // engine, sky light like the shared engine with both channels (decision E-389), with block light
    // from snapshots and flooded by the mesher alike.
    void mesherHandsOverTheSectionsLight()
    {
        const Fixture f;
        const auto chunks = f.chunks();
        QcLightEngine engine(f.table->lightTable(), {.sky = true});
        QcBlockLightEngine blockEngine(f.table);
        for (const auto &chunk : chunks) {
            engine.addChunk(*chunk);
            blockEngine.addChunk(chunk);
        }
        blockEngine.takeChanges();

        QcMeshInput input;
        input.chunks = chunks;
        input.renderTable = f.table;
        input.sectionIndex = 1;
        // Without a frame plan that reads it the mesh carries no section light.
        QVERIFY(QcChunkMesher::mesh(input).light.isUniform());
        input.sectionLight = true;
        const QcChunkMesh flooded = QcChunkMesher::mesh(input);
        for (std::size_t i = 0; i < 9; ++i) {
            input.blockLight[i] = std::make_shared<const QcChunkLight>(blockEngine.snapshot(chunks[i]->pos()));
        }
        input.hasBlockLight = true;
        const QcChunkMesh mesh = QcChunkMesher::mesh(input);
        QVERIFY(mesh.light.block && mesh.light.sky);

        int differentSky = 0;
        int differentBlock = 0;
        for (int y = 16; y < 32; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    const int index = QcChunkSection::index(x, y - 16, z);
                    differentSky += mesh.light.skyLevel(index) != engine.skyLight({x, y, z}) ? 1 : 0;
                    differentBlock += mesh.light.blockLevel(index) != engine.blockLight({x, y, z}) ? 1 : 0;
                    differentBlock += flooded.light.blockLevel(index) != engine.blockLight({x, y, z}) ? 1 : 0;
                }
            }
        }
        QCOMPARE(differentSky, 0);
        QCOMPARE(differentBlock, 0);

        // A section without blocks has no mesh and so no light.
        input.sectionIndex = 5;
        QVERIFY(QcChunkMesher::mesh(input).isEmpty());
    }

    // Sections uploaded into the pool and read back: a uniform section takes no slot, the others
    // fill a page and open the next, a released slot is reused.
    void poolHoldsTheTexelsOfEverySlot()
    {
        if (!m_rhi) {
            QSKIP("no Metal device for the offscreen QRhi");
        }
        QVERIFY(QcSectionLightPool::isSupported(m_rhi.get()));

        QcSectionLightPool pool(2);
        QList<QcSectionLight> lights;
        for (int k = 0; k < 3; ++k) {
            auto block = std::make_shared<QcLightNibbles>(0);
            auto sky = std::make_shared<QcLightNibbles>(0);
            for (int index = 0; index < QcLightNibbles::cellCount; ++index) {
                block->setLevel(index, (index + k) % 16);
                sky->setLevel(index, (index / 256 + 3 * k) % 16);
            }
            lights.append(QcSectionLight::fromChannels(block, sky));
        }

        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        QVERIFY(pool.update(m_rhi.get(), updates, 100, QcSectionLight{nullptr, nullptr, 0, 15}));
        for (int k = 0; k < 3; ++k) {
            QVERIFY(pool.update(m_rhi.get(), updates, quint64(k), lights[k]));
        }
        QRhiCommandBuffer *commands = nullptr;
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        commands->resourceUpdate(updates);
        m_rhi->endOffscreenFrame();

        const QcSectionLightPool::Slot uniform = *pool.slot(100);
        QVERIFY(uniform.isUniform());
        QCOMPARE(uniform.uniformSky, 15);
        QCOMPARE(pool.pageCount(), 2);
        QCOMPARE(pool.usedSlots(), 3);
        QCOMPARE(pool.bytes(), quint64(2 * 2 * 4096 * 2));
        for (int k = 0; k < 3; ++k) {
            const QcSectionLightPool::Slot slot = *pool.slot(quint64(k));
            QCOMPARE(slot.page, k / 2);
            QCOMPARE(slot.index, k % 2);
            QCOMPARE(readSlot(m_rhi.get(), pool.page(slot.page), slot.firstSlice()), lights[k].texels());
        }

        // Released and taken again; a section that turns uniform gives its slot back.
        pool.release(1);
        QVERIFY(!pool.slot(1));
        updates = m_rhi->nextResourceUpdateBatch();
        QVERIFY(pool.update(m_rhi.get(), updates, 7, lights[1]));
        QVERIFY(pool.update(m_rhi.get(), updates, 2, QcSectionLight{}));
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        commands->resourceUpdate(updates);
        m_rhi->endOffscreenFrame();
        QCOMPARE(pool.slot(7)->page, 0);
        QCOMPARE(pool.slot(7)->index, 1);
        QVERIFY(pool.slot(2)->isUniform());
        QCOMPARE(pool.usedSlots(), 2);

        pool.reset();
        QCOMPARE(pool.pageCount(), 0);
        QCOMPARE(pool.sections(), 0);
    }
};

QTEST_MAIN(TestSectionLight)
#include "test_section_light.moc"
