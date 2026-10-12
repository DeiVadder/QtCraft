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

void QcWorldView::setAtlas(const QList<QImage> &levels)
{
    m_atlas = levels;
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

void QcWorldView::setSelection(const QList<QcAabb> &boxes)
{
    if (m_selection != boxes) {
        m_selection = boxes;
        update();
    }
}

void QcWorldView::setEntityMesh(std::shared_ptr<const QcEntityMesh> mesh)
{
    m_entityMesh = std::move(mesh);
    update();
}

void QcWorldView::setHandMesh(std::shared_ptr<const QcEntityMesh> mesh)
{
    m_handMesh = std::move(mesh);
    update();
}

void QcWorldView::setSky(const QcSkyState &sky)
{
    m_sky = sky;
    update();
}

void QcWorldView::setFogRange(float start, float end)
{
    m_fogStart = start;
    m_fogEnd = end;
    update();
}

void QcWorldView::setDebugLines(std::shared_ptr<const QList<QcDebugLineVertex>> lines)
{
    if (lines || m_debugLines) {
        m_debugLines = std::move(lines);
        update();
    }
}
