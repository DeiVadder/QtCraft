#pragma once

#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/meshing/QcCapeTracker.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "common/core/QcBlockPos.h"
#include "common/physics/QcPhysics.h"

#include <QElapsedTimer>
#include <QString>

#include <memory>
#include <optional>
#include <utility>

class QcClientWorld;
class QcContentRegistry;
class QcSettingsController;
class QcTextureAtlas;
struct QcSessionParts;

// Builds the entity meshes of one frame from the running session: creatures, armour stands, carts
// with their load, drops, projectiles, flames, the local player seen from outside and the
// first-person arm, with their capes and the light they stand in. Hands nothing to the view; that
// is QcRenderBridge's part. Lives in the GUI thread.
class QcEntityFrameBuilder
{
public:
    QcEntityFrameBuilder(const QcSessionParts &parts, const QcSettingsController &settings);

    // Texture atlas of the running world's content; without one no entities are built.
    void setAtlas(std::shared_ptr<const QcTextureAtlas> atlas) { m_atlas = std::move(atlas); }
    // Test hook: freezes the local player's pose ("walk", "sprint", "swing" or "swing:<progress>", "swim", "jump",
    // "fall", "sneak", "death") for screenshots; the cape then rests on its target for that movement.
    void setForcedPose(const QString &pose) { m_forcedPose = pose; }

    // The local player's pose at `alpha` between the last two ticks, or the forced pose.
    QcEntityPose localPose(double alpha) const;
    // Creatures, dropped items, falling blocks and, seen from outside, the player; null without an atlas.
    [[nodiscard]] std::shared_ptr<QcEntityMesh> buildEntities(double alpha, QcCameraMode mode);
    // The first-person arm with the held item at `eye`; null without an atlas or while dead.
    [[nodiscard]] std::shared_ptr<QcEntityMesh> buildHand(double alpha, const QcVec3 &eye) const;

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

    void appendCreature(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame);
    void appendLocalPlayer(QcEntityMesh &mesh, const EntityFrame &frame);
    // Flames around a burning body of `width` and `height` standing at `feet` (decision E-393).
    static void appendFlames(QcEntityMesh &mesh, const EntityFrame &frame, const QcVec3 &feet, QcPhysics::BodySize body);
    // The block a container or hopper cart carries, inside the cart standing at `position`.
    void appendCartLoad(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame, const QcVec3 &position) const;
    // An armour stand in its pose with what it wears and holds (decision E-238).
    void appendStand(QcEntityMesh &mesh, const QcClientEntity &entity, const QcEntityMesher::Placement &placement, const EntityFrame &frame) const;
    // How the local player moves for the cape: its velocity, or the forced pose's movement.
    QcCapeDrive localCapeDrive(const QcEntityPose &pose) const;
    // Seconds since the previous frame's capes (at most QcCapeSwing::maxAdvance).
    double capeFrameSeconds();
    // Share of sky light at a position (0.35 under a roof, 1 in the open); a cheap stand-in for
    // sampling the meshes' sky light.
    static float skyExposure(const QcClientWorld &world, QcBlockPos pos);
    // Entities under a roof are dimmed by skyExposure().
    QcEntityLight entityLight(const QcClientWorld &world, const QcVec3 &position) const;
    // Block light (0..1) of entities and the hand at a position, as the meshes show it.
    float blockLightAt(QcBlockPos pos) const;
    QcEntityMesher::ModelLook playerLook(const QcAppearance &appearance) const;
    [[nodiscard]] std::optional<QcEntityMesher::ItemLook> heldLook(QcItemId item) const;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    QString m_forcedPose;
    // Cape swings of the drawn figures and the clock that advances them (decision E-240).
    QcCapeTracker m_capes;
    QElapsedTimer m_capeClock;
    // Vertex and index count of the last entity mesh.
    std::pair<qsizetype, qsizetype> m_entityMeshSize;
};
