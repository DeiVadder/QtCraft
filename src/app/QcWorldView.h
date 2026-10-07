#pragma once

#include "QcCamera.h"
#include "QcChunkMesh.h"

#include <QColor>
#include <QImage>
#include <QQuickRhiItem>
#include <QSet>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

// Qt Quick item that renders the voxel world with QRhi.
// All setters are called on the GUI thread; the renderer copies the state in synchronize().
class QcWorldView : public QQuickRhiItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldView)

public:
    struct MeshUpdate
    {
        quint64 key = 0;
        // Null means: remove the mesh with this key.
        std::shared_ptr<const QcChunkMesh> mesh;
    };

    explicit QcWorldView(QQuickItem *parent = nullptr);

    void setCamera(const QcCamera &camera);
    const QcCamera &camera() const { return m_camera; }

    void setAtlas(const QImage &atlas);
    void submitMesh(quint64 key, std::shared_ptr<const QcChunkMesh> mesh);
    void removeMesh(quint64 key);
    void clearMeshes();

    void setSkyColor(const QColor &color);
    void setDaylight(float daylight);
    void setFogRange(float start, float end);

    qsizetype meshCount() const { return m_meshKeys.size(); }

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    friend class QcWorldRenderer;

    QcCamera m_camera;
    QImage m_atlas;
    quint64 m_atlasVersion = 0;
    QList<MeshUpdate> m_pendingMeshUpdates;
    bool m_clearAllMeshes = false;
    QColor m_skyColor = QColor(135, 190, 235);
    float m_daylight = 1.0f;
    float m_fogStart = 80.0f;
    float m_fogEnd = 128.0f;
    QSet<quint64> m_meshKeys;
};
