#pragma once

#include "QcBlockPos.h"
#include "QcEntityMesher.h"
#include "QcViewCamera.h"

#include <QString>

#include <memory>
#include <optional>
#include <utility>

class QcClientWorld;
class QcContentRegistry;
class QcSettingsController;
class QcTextureAtlas;
struct QcSessionParts;

// Hands the running world to the world view once per frame: the camera, entity and hand meshes,
// the sky colour and the outline of the targeted block. Lives in the GUI thread.
class QcRenderBridge
{
public:
    QcRenderBridge(const QcSessionParts &parts, const QcSettingsController &settings);

    // Texture atlas of the running world's content; without one no entities are drawn.
    void setAtlas(std::shared_ptr<const QcTextureAtlas> atlas) { m_atlas = std::move(atlas); }
    // Test hook: freezes the local player's pose ("walk", "swing", "swim", "jump", "sneak", "death")
    // for screenshots.
    void setForcedPose(const QString &pose) { m_forcedPose = pose; }

    void placeCamera(const QcVec3 &eye, QcCameraMode mode);
    // Creatures, dropped items, falling blocks and, seen from outside, the player.
    void updateEntities(double alpha, QcCameraMode mode);
    // The first-person arm with the held item; hidden in the other camera modes.
    void updateHand(double alpha, const QcVec3 &eye, QcCameraMode mode);
    void updateSky(double alpha);
    void updateSelection();

    // The state a block item is drawn in (icons, hand, dropped items).
    static QcBlockId itemState(const QcContentRegistry &content, QcBlockId block);

private:
    // What every entity of one frame is drawn with.
    struct EntityFrame
    {
        const QcContentRegistry &content;
        const QcClientWorld &world;
        const QcEntityMesher::UvLookup &uv;
        double alpha = 0.0;
        double seconds = 0.0;
    };

    void appendCreature(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame) const;
    void appendLocalPlayer(QcEntityMesh &mesh, const EntityFrame &frame) const;
    void updateHandMesh(double alpha, const QcVec3 &eye);
    // Entities under a roof are dimmed; a cheap stand-in for sampling the light volume.
    QcEntityLight entityLight(const QcClientWorld &world, const QcVec3 &position) const;
    // Block light (0..1) of entities and the hand at a position, as the meshes show it.
    float blockLightAt(QcBlockPos pos) const;
    QcEntityMesher::ModelLook playerLook(const QcAppearance &appearance) const;
    QcEntityPose localPose(double alpha) const;
    std::optional<QcEntityMesher::ItemLook> heldLook(QcItemId item) const;
    static QcEntityMesher::ItemLook itemLook(const QcContentRegistry &content, QcItemId item);

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    QString m_forcedPose;
    // Vertex and index count of the last entity mesh.
    std::pair<qsizetype, qsizetype> m_entityMeshSize;
};
