// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/rendering/QcRenderSample.h"
#include "clientcore/camera/QcCamera.h"
#include "clientcore/debug/QcDebugLines.h"
#include "clientcore/entitymeshes/QcEntityMesh.h"
#include "clientcore/light/QcMovingLights.h"
#include "clientcore/light/QcSky.h"
#include "clientcore/maps/QcFramedMapAtlas.h"
#include "clientcore/meshing/QcChunkMesh.h"
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

class QcMaterialAtlas;

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
        // The QcMaterialAtlas::serial of the atlas's material maps (decision E-908), 0 without them.
        // Their pixels are handed to the renderer once, not copied with the frame (decision E-912).
        quint64 materials = 0;
        // Sky dome, fog colour and the daylight factor of the shaders.
        QcSkyState sky = QcSky::at(QcDayCycle::startTime);
        float fogStart = 80.0f;
        float fogEnd = 128.0f;
        // Most samples along a slanted footprint (decision E-721); 1 is plain mip mapping.
        int anisotropicSamples = QcGameSettings::defaultTextureFiltering;
        // Classic or realistic mode with its quality and exposure (decision E-899).
        QcRenderOptions render;
        // How far the camera is under the open sky (1) or deep in a cave (0), eased; the height fog
        // of the realistic mode fades with it, so caves stay clear.
        float outdoors = 1.0f;
        // The water of the realistic mode (decision E-906): whether the camera is under water, and
        // the waves' time in seconds (QcWaterLook::waveSeconds, held with the animation tick).
        bool underwater = false;
        float waterSeconds = 0.0f;
        // Torches in hands, fireflies (decision E-902): drawn by the realistic mode only, at most
        // QcRenderOptions::movingLights() of them; world positions.
        QList<QcMovingLight> movingLights;
        // Outlined boxes of the targeted block in world space; empty for none.
        QList<QcAabb> selection;
        std::shared_ptr<const QcEntityMesh> entityMesh;
        std::shared_ptr<const QcEntityMesh> handMesh;
        // The maps in item frames and the tiles of the texture they are drawn with (decision E-809).
        std::shared_ptr<const QcEntityMesh> framedMapMesh;
        std::shared_ptr<const QList<QcFramedMapAtlas::Tile>> framedMapTiles;
        // The debug lines and the precipitation, each relative to its origin (decision E-883).
        std::shared_ptr<const QList<QcDebugLineVertex>> debugLines;
        QcVec3 debugLinesOrigin;
        std::shared_ptr<const QList<QcPrecipitationDrop>> precipitation;
        QcVec3 precipitationOrigin;
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
    // The material maps of the atlas set last (QcTextureStackBuild::Result::materials), null
    // without them. Handed over to the renderer, which lets them go once they are on the GPU
    // (decision E-912).
    void setMaterials(std::shared_ptr<const QcMaterialAtlas> materials);
    // The time of the animated textures in ticks (decision E-878), QcAnimationClock::ticks(); the
    // test hook QTCRAFT_ANIMATION_TICK holds it at a fixed tick.
    void setAnimationTime(double ticks);
    // The animation time of the last setAnimationTime as the block shader gets it (wrapped, or the
    // tick the test hook holds): entity meshes pick their frames at it (decision E-919).
    [[nodiscard]] double animationTicks() const { return m_frame.animationTicks; }
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
    // Render mode, quality and exposure (decision E-899).
    void setRenderOptions(QcRenderOptions options);
    [[nodiscard]] const QcRenderOptions &renderOptions() const { return m_frame.render; }
    // The moving lights of this frame (FrameInput::movingLights).
    void setMovingLights(QList<QcMovingLight> lights);
    // How far the camera is under the open sky, 0..1 (FrameInput::outdoors).
    void setOutdoors(float outdoors);
    // Whether the camera is under water (FrameInput::underwater).
    void setUnderwater(bool underwater);
    // Debug visualisations for this frame (decision E-225), relative to `origin`; null or empty
    // draws none.
    void setDebugLines(std::shared_ptr<const QList<QcDebugLineVertex>> lines, const QcVec3 &origin);
    // Rain and snow for this frame, one instance per particle (QcPrecipitationField), relative to
    // `origin`; null or empty draws none.
    void setPrecipitation(std::shared_ptr<const QList<QcPrecipitationDrop>> drops, const QcVec3 &origin);

    qsizetype meshCount() const { return m_meshKeys.size(); }

    // Benchmark: while enabled, the renderer hands over one QcRenderSample per rendered frame.
    void setCollectRenderSamples(bool collect) { m_frame.collectRenderSamples = collect; }
    QList<QcRenderSample> takeRenderSamples() { return std::exchange(m_renderSamples, {}); }

signals:
    // The sections handed to the renderer are no longer on the GPU (a new renderer or a new QRhi);
    // the owner of the meshes submits all of them again (QcMeshScheduler::remeshAll). Emitted on
    // the GUI thread.
    void meshesLost();
    // The material maps handed to the renderer are no longer on the GPU (a new renderer or a new
    // QRhi) and it keeps no copy: the owner builds them again (QcTexturePackController::
    // rebuildMaterials). Emitted on the GUI thread.
    void materialsLost();

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
    // Render thread, GUI thread blocked: queues materialsLost() for the GUI thread.
    void materialsLostOnDevice();

    FrameInput m_frame;
    int m_animationCycleTicks = 1;
    // QTCRAFT_ANIMATION_TICK: the animation time of every frame (screenshots of chosen frames).
    std::optional<double> m_fixedAnimationTicks;
    // Handed over, not copied: the renderer takes the updates and keeps the list's capacity.
    QList<MeshUpdate> m_pendingMeshUpdates;
    bool m_clearAllMeshes = false;
    // The material maps set last, until the renderer takes them (decision E-912).
    std::shared_ptr<const QcMaterialAtlas> m_pendingMaterials;
    QSet<quint64> m_meshKeys;
    // Renderers created so far: a later one starts without the sections of the earlier.
    int m_renderers = 0;
    QList<QcRenderSample> m_renderSamples;
};
