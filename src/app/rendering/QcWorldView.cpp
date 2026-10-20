#include "app/rendering/QcWorldView.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcWorldRenderer.h"

QcWorldView::QcWorldView(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
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

void QcWorldView::setAtlas(const QList<QImage> &levels)
{
    m_frame.atlas = levels;
    ++m_frame.atlasVersion;
    update();
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

void QcWorldView::setDebugLines(std::shared_ptr<const QList<QcDebugLineVertex>> lines)
{
    if (lines || m_frame.debugLines) {
        m_frame.debugLines = std::move(lines);
        update();
    }
}

void QcWorldView::setPrecipitation(std::shared_ptr<const QList<QcPrecipitationDrop>> drops)
{
    if (drops || m_frame.precipitation) {
        m_frame.precipitation = std::move(drops);
        update();
    }
}
