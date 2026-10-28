#include "app/rendering/QcWorldView.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcWorldRenderer.h"
#include "clientcore/light/QcWaterLook.h"

#include <algorithm>
#include <cmath>

QcWorldView::QcWorldView(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    bool fixed = false;
    const double ticks = qEnvironmentVariable("QTCRAFT_ANIMATION_TICK").toDouble(&fixed);
    if (fixed && ticks >= 0.0) {
        m_fixedAnimationTicks = ticks;
        qCInfo(lcRenderer) << "World view: animated textures held at tick" << ticks;
    }
}

QQuickRhiItemRenderer *QcWorldView::createRenderer()
{
    // A renderer replacing another (Qt Quick dropped the scene graph node) has none of the sections
    // the old one had taken; the pending updates are still here and go to the new one.
    ++m_renderers;
    qCInfo(lcRenderer) << "World view: renderer" << m_renderers << "created";
    if (m_renderers > 1) {
        meshesLostOnDevice();
    }
    return new QcWorldRenderer;
}

void QcWorldView::meshesLostOnDevice()
{
    if (!m_meshKeys.isEmpty()) {
        qCInfo(lcRenderer) << "World view: the GPU lost" << m_meshKeys.size() << "sections, meshing them again";
        QMetaObject::invokeMethod(this, &QcWorldView::meshesLost, Qt::QueuedConnection);
    }
}

void QcWorldView::setCamera(const QcCamera &camera)
{
    m_frame.camera = camera;
    update();
}

void QcWorldView::setAtlas(const QList<QImage> &levels, const QImage &animationMap, int animationCycleTicks)
{
    m_frame.atlas = levels;
    m_frame.animationMap = animationMap;
    m_animationCycleTicks = std::max(1, animationCycleTicks);
    ++m_frame.atlasVersion;
    update();
}

void QcWorldView::setMaterials(std::shared_ptr<const QcMaterialAtlas> materials)
{
    m_frame.materials = std::move(materials);
    update();
}

void QcWorldView::setAnimationTime(double ticks)
{
    // Wrapped in double, so the float the shader gets stays exact however long the game runs.
    const double time = std::fmod(std::max(0.0, m_fixedAnimationTicks.value_or(ticks)), static_cast<double>(m_animationCycleTicks));
    m_frame.animationTicks = static_cast<float>(time);
    m_frame.waterSeconds = QcWaterLook::waveSeconds(m_fixedAnimationTicks.value_or(ticks));
}

void QcWorldView::submitMesh(quint64 key, std::shared_ptr<const QcChunkMesh> mesh)
{
    if (!mesh || mesh->isEmpty()) {
        removeMesh(key);
        return;
    }

    m_meshKeys.insert(key);
    m_pendingMeshUpdates.append({key, std::move(mesh)});
    update();
}

void QcWorldView::removeMesh(quint64 key)
{
    if (!m_meshKeys.remove(key)) {
        return;
    }

    m_pendingMeshUpdates.append({key, nullptr});
    update();
}

void QcWorldView::clearMeshes()
{
    m_meshKeys.clear();
    m_pendingMeshUpdates.clear();
    m_clearAllMeshes = true;
    update();
}

void QcWorldView::setSelection(const QList<QcAabb> &boxes)
{
    if (m_frame.selection != boxes) {
        m_frame.selection = boxes;
        update();
    }
}

void QcWorldView::setEntityMesh(std::shared_ptr<const QcEntityMesh> mesh)
{
    m_frame.entityMesh = std::move(mesh);
    update();
}

void QcWorldView::setFramedMaps(std::shared_ptr<const QcEntityMesh> mesh, std::shared_ptr<const QList<QcFramedMapAtlas::Tile>> tiles)
{
    m_frame.framedMapMesh = std::move(mesh);
    m_frame.framedMapTiles = std::move(tiles);
    update();
}

void QcWorldView::setHandMesh(std::shared_ptr<const QcEntityMesh> mesh)
{
    m_frame.handMesh = std::move(mesh);
    update();
}

void QcWorldView::setSky(const QcSkyState &sky)
{
    m_frame.sky = sky;
    update();
}

void QcWorldView::setFogRange(float start, float end)
{
    m_frame.fogStart = start;
    m_frame.fogEnd = end;
    update();
}

void QcWorldView::setAnisotropicSamples(int samples)
{
    if (m_frame.anisotropicSamples != samples) {
        m_frame.anisotropicSamples = samples;
        update();
    }
}

void QcWorldView::setRenderOptions(QcRenderOptions options)
{
    if (m_frame.render != options) {
        m_frame.render = options;
        update();
    }
}

void QcWorldView::setMovingLights(QList<QcMovingLight> lights)
{
    if (m_frame.movingLights != lights) {
        m_frame.movingLights = std::move(lights);
        update();
    }
}

void QcWorldView::setOutdoors(float outdoors)
{
    if (m_frame.outdoors != outdoors) {
        m_frame.outdoors = outdoors;
        update();
    }
}

void QcWorldView::setUnderwater(bool underwater)
{
    if (m_frame.underwater != underwater) {
        m_frame.underwater = underwater;
        update();
    }
}

void QcWorldView::setDebugLines(std::shared_ptr<const QList<QcDebugLineVertex>> lines, const QcVec3 &origin)
{
    if (lines || m_frame.debugLines) {
        m_frame.debugLines = std::move(lines);
        m_frame.debugLinesOrigin = origin;
        update();
    }
}

void QcWorldView::setPrecipitation(std::shared_ptr<const QList<QcPrecipitationDrop>> drops, const QcVec3 &origin)
{
    if (drops || m_frame.precipitation) {
        m_frame.precipitation = std::move(drops);
        m_frame.precipitationOrigin = origin;
        update();
    }
}
