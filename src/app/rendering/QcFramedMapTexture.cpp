#include "app/rendering/QcFramedMapTexture.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcAtlasTexture.h"

bool QcFramedMapTexture::create(QRhi *rhi, QRhiResourceUpdateBatch *updates, QRhiBuffer *frameUniforms)
{
    m_tried = true;
    const int side = QcFramedMapAtlas::side;
    m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(side, side)));
    if (!m_texture->create()) {
        qCWarning(lcRenderer) << "The GPU refused the texture of the framed maps; they are not drawn";
        m_texture.reset();
        return false;
    }
    m_sampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge));
    m_sampler->create();
    // Binding 2 as in the atlas bindings (QcWorldDeviceResources::atlasBindings), with an animation
    // map that moves nothing: the entity pipeline is built with those and needs their layout.
    m_stillAnimation = QcAtlasTexture::createAnimationMap(rhi, QImage(), updates);
    if (!m_stillAnimation) {
        m_texture.reset();
        return false;
    }
    m_stillSampler = QcAtlasTexture::createAnimationMapSampler(rhi);
    m_bindings.reset(rhi->newShaderResourceBindings());
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                 frameUniforms),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_texture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::VertexStage, m_stillAnimation.get(), m_stillSampler.get()),
    });
    m_bindings->create();
    m_uploaded = QList<quint64>(QcFramedMapAtlas::tileCount, 0);
    return true;
}

quint32 QcFramedMapTexture::upload(QRhi *rhi, QRhiResourceUpdateBatch *updates, const QList<QcFramedMapAtlas::Tile> &tiles,
                                   QRhiBuffer *frameUniforms)
{
    if (!m_texture && (m_tried || !create(rhi, updates, frameUniforms))) {
        return 0;
    }

    QList<QRhiTextureUploadEntry> entries;
    quint32 bytes = 0;
    for (qsizetype index = 0; index < tiles.size() && index < m_uploaded.size(); ++index) {
        const QcFramedMapAtlas::Tile &tile = tiles.at(index);
        if (tile.image.isNull() || tile.version == m_uploaded.at(index)) {
            continue;
        }
        QRhiTextureSubresourceUploadDescription description(tile.image);
        const int column = static_cast<int>(index) % QcFramedMapAtlas::columns;
        const int row = static_cast<int>(index) / QcFramedMapAtlas::columns;
        description.setDestinationTopLeft(QPoint(column * QcMapData::size, row * QcMapData::size));
        entries.append(QRhiTextureUploadEntry(0, 0, description));
        bytes += static_cast<quint32>(tile.image.sizeInBytes());
        m_uploaded[index] = tile.version;
    }
    if (!entries.isEmpty()) {
        QRhiTextureUploadDescription description;
        description.setEntries(entries.cbegin(), entries.cend());
        updates->uploadTexture(m_texture.get(), description);
    }
    return bytes;
}
