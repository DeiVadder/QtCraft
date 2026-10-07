#include "QcWorldView.h"

#include "QcWorldRenderer.h"

QcWorldView::QcWorldView(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
}

QQuickRhiItemRenderer *QcWorldView::createRenderer()
{
    return new QcWorldRenderer;
}

void QcWorldView::setCamera(const QcCamera &camera)
{
    m_camera = camera;
    update();
}

void QcWorldView::setAtlas(const QImage &atlas)
{
    m_atlas = atlas;
    ++m_atlasVersion;
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

void QcWorldView::setSkyColor(const QColor &color)
{
    m_skyColor = color;
    update();
}

void QcWorldView::setDaylight(float daylight)
{
    m_daylight = daylight;
    update();
}

void QcWorldView::setFogRange(float start, float end)
{
    m_fogStart = start;
    m_fogEnd = end;
    update();
}
