#include "app/rendering/QcMaterialTextures.h"
#include "app/rendering/QcRenderPipelines.h"
#include "app/rendering/QcShadowMap.h"
#include "app/rendering/QcWorldRendererResources.h"
#include "clientcore/maps/QcFramedMapAtlas.h"
#include "clientcore/materials/QcMaterialAtlas.h"
#include "clientcore/settings/QcRenderOptions.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QImage>
#include <QLoggingCategory>
#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

using namespace Qt::StringLiterals;

// The material maps of the realistic mode on QRhi's Null backend (decision E-908): flat textures
// stand in while there are no maps (quality Low, no maps built), the maps replace them only when
// wanted, release() and the classic mode leave nothing on the GPU, and the receivers' bindings
// with the maps fit the entity pipeline built with them together with the framed maps' bindings
// (decision E-896) whichever textures stand at 5 and 6. The receiver shaders are baked into the
// test (tests/client/tests.cmake).
class TestMaterialTextures : public QObject
{
    Q_OBJECT

    std::unique_ptr<QRhi> m_rhi;
    std::shared_ptr<const QcMaterialAtlas> m_maps;

    QcWorldDeviceResources device(QRhiResourceUpdateBatch *updates) const
    {
        QcWorldDeviceResources result = QcWorldDeviceResources::create(m_rhi.get());
        QImage atlas(10, 10, QImage::Format_RGBA8888);
        atlas.fill(Qt::gray);
        QImage animations(2, 2, QImage::Format_RGBA8888);
        animations.fill(Qt::transparent);
        result.replaceAtlas({atlas}, animations, 1, updates);
        return result;
    }

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
        QcTextureAtlas::Options options;
        options.materials = true;
        m_maps = QcTextureAtlas::build(*QcTestUtil::baseContent()->registry, {}, options).materials();
        QVERIFY(m_maps);
    }

    void flatUntilMapsAreWanted()
    {
        QcMaterialTextures materials;
        QVERIFY(!materials.holdsResources());
        QVERIFY(materials.entries().isEmpty());
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();

        // Quality Low: the flat textures only.
        QVERIFY(materials.prepare(m_rhi.get(), m_maps, false, updates));
        QVERIFY(materials.holdsResources());
        QVERIFY(!materials.hasMaps());
        QCOMPARE(materials.bytes(), 0);
        const QList<QRhiShaderResourceBinding> flat = materials.entries();
        QCOMPARE(flat.size(), 2);
        QVERIFY(!materials.replaces(m_rhi.get(), m_maps, false));
        QVERIFY(!materials.replaces(m_rhi.get(), nullptr, true));

        // Medium: the maps, the same again changes nothing.
        QVERIFY(materials.replaces(m_rhi.get(), m_maps, true));
        QVERIFY(materials.prepare(m_rhi.get(), m_maps, true, updates));
        QVERIFY(materials.hasMaps());
        QCOMPARE(materials.bytes(), m_maps->bytes());
        QVERIFY(!materials.replaces(m_rhi.get(), m_maps, true));
        QVERIFY(materials.entries() != flat);

        // Back to Low and then classic: the maps go, then everything.
        QVERIFY(materials.replaces(m_rhi.get(), m_maps, false));
        QVERIFY(materials.prepare(m_rhi.get(), m_maps, false, updates));
        QVERIFY(!materials.hasMaps());
        materials.release();
        QVERIFY(!materials.holdsResources());
        QVERIFY(materials.entries().isEmpty());
        updates->release();
    }

    // The receivers' bindings with the material entries extend the atlas layout to 7 entries; the
    // entity pipeline built with them draws the framed maps with framedMapBindings() of the same
    // entries, with the flat textures as with the maps.
    void receiverBindingsFitWithTheMaps()
    {
        QRhiCommandBuffer *commands = nullptr;
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        QcWorldDeviceResources resources = device(updates);
        QImage map(QcMapData::size, QcMapData::size, QImage::Format_RGBA8888);
        map.fill(Qt::darkGreen);
        QList<QcFramedMapAtlas::Tile> tiles(QcFramedMapAtlas::tileCount);
        tiles[0] = {map, 1};
        QVERIFY(resources.framedMaps.upload(m_rhi.get(), updates, tiles, resources.frameUniforms.get()) > 0);

        QcShadowMap shadows;
        QVERIFY(shadows.prepare(m_rhi.get(), QcRenderOptions{.mode = QcRenderOptions::Mode::Realistic}.shadows()));
        QcMaterialTextures materials;
        QVERIFY(materials.prepare(m_rhi.get(), m_maps, false, updates));

        std::unique_ptr<QRhiTexture> colour(m_rhi->newTexture(QRhiTexture::RGBA8, {64, 64}, 1, QRhiTexture::RenderTarget));
        QVERIFY(colour->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(m_rhi->newTextureRenderTarget({QRhiColorAttachment(colour.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        QVERIFY(target->create());
        const QLoggingCategory category("test");

        for (const bool withMaps : {false, true}) {
            if (materials.replaces(m_rhi.get(), m_maps, withMaps)) {
                shadows.releaseAtlasBindings();
            }
            QVERIFY(materials.prepare(m_rhi.get(), m_maps, withMaps, updates));
            QCOMPARE(materials.hasMaps(), withMaps);
            QRhiShaderResourceBindings *const receivers = shadows.receiverBindings(resources, materials.entries());
            QVERIFY(receivers);
            QCOMPARE(receivers->cbeginBindings() + 7, receivers->cendBindings());
            const std::unique_ptr<QRhiGraphicsPipeline> receiving = QcRenderPipelines::createEntityPipeline(
                m_rhi.get(), target.get(), receivers, QcRenderPipelines::loadShader(u":/shaders/entity_shadow.vert.qsb"_s, category),
                QcRenderPipelines::loadShader(u":/shaders/entity_shadow.frag.qsb"_s, category));
            QVERIFY(receiving);
            QRhiShaderResourceBindings *const framed = shadows.framedMapBindings(resources, materials.entries());
            QVERIFY(framed);
            QVERIFY(receiving->shaderResourceBindings()->isLayoutCompatible(framed));
            // Without the material entries the framed maps would not fit (decision E-896).
            QVERIFY(!receivers->isLayoutCompatible(resources.framedMaps.bindings()));
        }
        shadows.releaseAtlasBindings();
        updates->release();
        QCOMPARE(m_rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    }
};

QTEST_GUILESS_MAIN(TestMaterialTextures)
#include "test_material_textures.moc"
