#pragma once

#include "QcCamera.h"
#include "QcAabb.h"
#include "QcBlockPos.h"
#include "QcChunkMesh.h"
#include "QcDebugLines.h"
#include "QcEntityMesh.h"
#include "QcPrecipitationField.h"
#include "QcRenderSample.h"
#include "QcSky.h"
#include "QcDayCycle.h"

#include <QColor>
#include <QImage>
#include <QQuickRhiItem>
#include <QSet>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>
#include <utility>

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

    // The atlas's mip levels, level 0 first (QcTextureAtlas::levels()).
    void setAtlas(const QList<QImage> &levels);
    void submitMesh(quint64 key, std::shared_ptr<const QcChunkMesh> mesh);
    void removeMesh(quint64 key);
    void clearMeshes();

    // Block outlined as the player's target; std::nullopt hides the outline.
    // Outlined boxes of the targeted block in world space; empty for none.
    void setSelection(const QList<QcAabb> &boxes);
    // Entity triangles for this frame (replaces the previous set).
    void setEntityMesh(std::shared_ptr<const QcEntityMesh> mesh);
    // First-person arm and held item; drawn in front of everything else. Null hides it.
    void setHandMesh(std::shared_ptr<const QcEntityMesh> mesh);
    // Sky dome, fog colour (its horizon) and the daylight factor of the shaders.
    void setSky(const QcSkyState &sky);
    void setFogRange(float start, float end);
    // Debug visualisations for this frame (decision E-225); null or empty draws none.
    void setDebugLines(std::shared_ptr<const QList<QcDebugLineVertex>> lines);
    // Rain and snow for this frame, one instance per particle (QcPrecipitationField); null or empty
    // draws none.
    void setPrecipitation(std::shared_ptr<const QList<QcPrecipitationDrop>> drops);

    qsizetype meshCount() const { return m_meshKeys.size(); }

    // Benchmark: while enabled, the renderer hands over one QcRenderSample per rendered frame.
    void setCollectRenderSamples(bool collect) { m_collectRenderSamples = collect; }
    QList<QcRenderSample> takeRenderSamples() { return std::exchange(m_renderSamples, {}); }

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    friend class QcWorldRenderer;

    QcCamera m_camera;
    QList<QImage> m_atlas;
    quint64 m_atlasVersion = 0;
    QList<MeshUpdate> m_pendingMeshUpdates;
    bool m_clearAllMeshes = false;
    QcSkyState m_sky = QcSky::at(QcDayCycle::startTime);
    float m_fogStart = 80.0f;
    float m_fogEnd = 128.0f;
    QSet<quint64> m_meshKeys;
    QList<QcAabb> m_selection;
    std::shared_ptr<const QcEntityMesh> m_entityMesh;
    std::shared_ptr<const QcEntityMesh> m_handMesh;
    std::shared_ptr<const QList<QcDebugLineVertex>> m_debugLines;
    std::shared_ptr<const QList<QcPrecipitationDrop>> m_precipitation;
    bool m_collectRenderSamples = false;
    QList<QcRenderSample> m_renderSamples;
};
