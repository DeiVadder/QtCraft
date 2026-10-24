#pragma once

#include "app/rendering/QcRenderSample.h"
#include "clientcore/camera/QcCamera.h"
#include "clientcore/debug/QcDebugLines.h"
#include "clientcore/light/QcSky.h"
#include "clientcore/maps/QcFramedMapAtlas.h"
#include "clientcore/meshing/QcChunkMesh.h"
#include "clientcore/meshing/QcEntityMesh.h"
#include "clientcore/settings/QcGameSettings.h"
#include "clientcore/weather/QcPrecipitationField.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"
#include "common/world/QcDayCycle.h"

#include <QColor>
#include <QImage>
#include <QQuickRhiItem>
#include <QSet>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>
#include <utility>

// Qt Quick item that renders the voxel world with QRhi.
// All setters are called on the GUI thread. QcWorldRenderer::synchronize() runs on the render thread
// while Qt Quick blocks the GUI thread: it copies the FrameInput snapshot and takes the queued mesh
// updates and the collected benchmark samples; nothing else of the view is read on the render thread.
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

    // The state a frame is drawn from, as one snapshot: the setters change it on the GUI thread and
    // the renderer copies it as a whole in synchronize() (decision E-710). Small to copy by value:
    // lists and meshes are implicitly or explicitly shared, a copy allocates nothing.
    struct FrameInput
    {
        QcCamera camera;
        // The atlas's mip levels and a counter that changes with every setAtlas().
        QList<QImage> atlas;
        quint64 atlasVersion = 0;
        // The atlas's animations (QcTextureAtlas::animationMap, decision E-878), uploaded with it,
        // and the animation time in ticks, wrapped where every animation starts over.
        QImage animationMap;
        float animationTicks = 0.0f;
        // Sky dome, fog colour and the daylight factor of the shaders.
        QcSkyState sky = QcSky::at(QcDayCycle::startTime);
        float fogStart = 80.0f;
        float fogEnd = 128.0f;
        // Most samples along a slanted footprint (decision E-721); 1 is plain mip mapping.
        int anisotropicSamples = QcGameSettings::defaultTextureFiltering;
        // Outlined boxes of the targeted block in world space; empty for none.
        QList<QcAabb> selection;
        std::shared_ptr<const QcEntityMesh> entityMesh;
        std::shared_ptr<const QcEntityMesh> handMesh;
        // The maps in item frames and the tiles of the texture they are drawn with (decision E-809).
        std::shared_ptr<const QcEntityMesh> framedMapMesh;
        std::shared_ptr<const QList<QcFramedMapAtlas::Tile>> framedMapTiles;
        std::shared_ptr<const QList<QcDebugLineVertex>> debugLines;
        std::shared_ptr<const QList<QcPrecipitationDrop>> precipitation;
        // Benchmark: the renderer records one QcRenderSample per frame while set.
        bool collectRenderSamples = false;
    };

    explicit QcWorldView(QQuickItem *parent = nullptr);

    void setCamera(const QcCamera &camera);
    const QcCamera &camera() const { return m_frame.camera; }

    // The atlas's mip levels, level 0 first (QcTextureAtlas::levels()), its animations
    // (QcTextureAtlas::animationMap) and the ticks after which they all start over
    // (QcTextureAtlas::animationCycleTicks).
    void setAtlas(const QList<QImage> &levels, const QImage &animationMap = {}, int animationCycleTicks = 1);
    // The time of the animated textures in ticks (decision E-878), QcAnimationClock::ticks(); the
    // test hook QTCRAFT_ANIMATION_TICK holds it at a fixed tick.
    void setAnimationTime(double ticks);
    void submitMesh(quint64 key, std::shared_ptr<const QcChunkMesh> mesh);
    void removeMesh(quint64 key);
    void clearMeshes();

    // Outlined boxes of the targeted block in world space; empty for none.
    void setSelection(const QList<QcAabb> &boxes);
    // Entity triangles for this frame (replaces the previous set).
    void setEntityMesh(std::shared_ptr<const QcEntityMesh> mesh);
    // The maps in item frames for this frame and the tiles of their texture (decision E-809).
    void setFramedMaps(std::shared_ptr<const QcEntityMesh> mesh, std::shared_ptr<const QList<QcFramedMapAtlas::Tile>> tiles);
    // First-person arm and held item; drawn in front of everything else. Null hides it.
    void setHandMesh(std::shared_ptr<const QcEntityMesh> mesh);
    // Sky dome, fog colour (its horizon) and the daylight factor of the shaders.
    void setSky(const QcSkyState &sky);
    void setFogRange(float start, float end);
    // Texture filtering of the blocks (decision E-721): 1, 2, 4 or 8 samples.
    void setAnisotropicSamples(int samples);
    // Debug visualisations for this frame (decision E-225); null or empty draws none.
    void setDebugLines(std::shared_ptr<const QList<QcDebugLineVertex>> lines);
    // Rain and snow for this frame, one instance per particle (QcPrecipitationField); null or empty
    // draws none.
    void setPrecipitation(std::shared_ptr<const QList<QcPrecipitationDrop>> drops);

    qsizetype meshCount() const { return m_meshKeys.size(); }

    // Benchmark: while enabled, the renderer hands over one QcRenderSample per rendered frame.
    void setCollectRenderSamples(bool collect) { m_frame.collectRenderSamples = collect; }
    QList<QcRenderSample> takeRenderSamples() { return std::exchange(m_renderSamples, {}); }

signals:
    // The sections handed to the renderer are no longer on the GPU (a new renderer or a new QRhi);
    // the owner of the meshes submits all of them again (QcMeshScheduler::remeshAll). Emitted on
    // the GUI thread.
    void meshesLost();

private:
    // Called by Qt Quick on the render thread; Qt Quick owns the returned renderer and deletes it
    // there when the item or its scene graph goes away. Private on purpose: only Qt calls it.
    [[nodiscard]] QQuickRhiItemRenderer *createRenderer() final; // NOLINT(misc-override-with-different-visibility)
    // QcWorldRenderer::synchronize() copies m_frame, takes the mesh updates and appends its samples
    // to m_renderSamples, while the GUI thread is blocked.
    friend class QcWorldRenderer;
    // Render thread, GUI thread blocked: queues meshesLost() for the GUI thread, when sections were
    // submitted.
    void meshesLostOnDevice();

    FrameInput m_frame;
    int m_animationCycleTicks = 1;
    // QTCRAFT_ANIMATION_TICK: the animation time of every frame (screenshots of chosen frames).
    std::optional<double> m_fixedAnimationTicks;
    // Handed over, not copied: the renderer takes the updates and keeps the list's capacity.
    QList<MeshUpdate> m_pendingMeshUpdates;
    bool m_clearAllMeshes = false;
    QSet<quint64> m_meshKeys;
    // Renderers created so far: a later one starts without the sections of the earlier.
    int m_renderers = 0;
    QList<QcRenderSample> m_renderSamples;
};
