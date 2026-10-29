// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcWorldRendererResources.h"
#include "clientcore/maps/QcFramedMapAtlas.h"

#include <QTest>

#include <memory>

// The bindings of the framed maps (decision E-809) have the layout of the atlas bindings, which the
// entity pipeline that draws them is built with (decision E-896): with a binding less, Metal binds
// the vertex buffers to other slots and the framed-map draw reads the draw origins as vertices, a
// large triangle in garbage colours. The resources live on a QRhi of the Null backend (no GPU).
class TestFramedMapTexture : public QObject
{
    Q_OBJECT

    std::unique_ptr<QRhi> m_rhi;

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
    }

    void framedMapBindingsHaveTheAtlasLayout()
    {
        QcWorldDeviceResources device = QcWorldDeviceResources::create(m_rhi.get());
        QRhiCommandBuffer *commands = nullptr;
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();

        // One cell of the last mip level (QcAtlasLayout::gridOf).
        QImage atlas(10, 10, QImage::Format_RGBA8888);
        atlas.fill(Qt::gray);
        QImage animations(2, 2, QImage::Format_RGBA8888);
        animations.fill(Qt::transparent);
        device.replaceAtlas({atlas}, animations, 1, updates);
        QVERIFY(device.hasAtlas());
        QRhiShaderResourceBindings *atlasBindings = device.atlasBindings();
        QVERIFY(atlasBindings);

        QImage map(QcMapData::size, QcMapData::size, QImage::Format_RGBA8888);
        map.fill(Qt::darkGreen);
        QList<QcFramedMapAtlas::Tile> tiles(QcFramedMapAtlas::tileCount);
        tiles[0] = {map, 1};
        QVERIFY(device.framedMaps.upload(m_rhi.get(), updates, tiles, device.frameUniforms.get()) > 0);
        QRhiShaderResourceBindings *framed = device.framedMaps.bindings();
        QVERIFY(framed);

        // Same bindings, types and stages in both directions: the entity pipeline built with the atlas
        // bindings may draw with the framed maps' ones.
        QVERIFY(framed->isLayoutCompatible(atlasBindings));
        QVERIFY(atlasBindings->isLayoutCompatible(framed));

        // The check tells layouts apart on this backend: the frame uniforms and the texture alone,
        // as the framed maps had them before, do not fit.
        std::unique_ptr<QRhiShaderResourceBindings> twoBindings(m_rhi->newShaderResourceBindings());
        twoBindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                     device.frameUniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, device.atlasTexture.get(),
                                                      device.atlasSampler.get()),
        });
        QVERIFY(twoBindings->create());
        QVERIFY(!twoBindings->isLayoutCompatible(atlasBindings));

        commands->resourceUpdate(updates);
        m_rhi->endOffscreenFrame();
    }
};

QTEST_GUILESS_MAIN(TestFramedMapTexture)
#include "test_framed_map_texture.moc"
