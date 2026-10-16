#pragma once

#include "QcBlockPos.h"
#include "QcCapeTracker.h"
#include "QcEntityMesher.h"
#include "QcFovEffect.h"
#include "QcPhysics.h"
#include "QcPrecipitationField.h"
#include "QcViewCamera.h"

#include <QElapsedTimer>
#include <QString>

#include <array>
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
    // Test hook: freezes the local player's pose ("walk", "sprint", "swing" or "swing:<progress>", "swim", "jump",
    // "fall", "sneak", "death") for screenshots; the cape then rests on its target for that movement.
    void setForcedPose(const QString &pose) { m_forcedPose = pose; }

    // The camera at the eye, with the field of view widened while sprinting (decision E-280);
    // `frameSeconds` advances the widening's easing.
    void placeCamera(const QcVec3 &eye, QcCameraMode mode, double frameSeconds);
    // Creatures, dropped items, falling blocks and, seen from outside, the player.
    void updateEntities(double alpha, QcCameraMode mode);
    // The first-person arm with the held item; hidden in the other camera modes.
    void updateHand(double alpha, const QcVec3 &eye, QcCameraMode mode);
    // After placeCamera: the sky of the time of day under the weather and a lightning flash; below
    // the world it darkens with the camera height.
    void updateSky(double alpha);
    // Rain and snow around the eye, advanced by `frameSeconds` (decision E-395).
    void updateWeather(const QcVec3 &eye, double frameSeconds);
    void updateSelection();

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
    void updateHandMesh(double alpha, const QcVec3 &eye);
    // Share of sky light at a position (0.35 under a roof, 1 in the open); a cheap stand-in for
    // sampling the meshes' sky light.
    static float skyExposure(const QcClientWorld &world, QcBlockPos pos);
    // Entities under a roof are dimmed by skyExposure().
    QcEntityLight entityLight(const QcClientWorld &world, const QcVec3 &position) const;
    // Block light (0..1) of entities and the hand at a position, as the meshes show it.
    float blockLightAt(QcBlockPos pos) const;
    QcEntityMesher::ModelLook playerLook(const QcAppearance &appearance) const;
    QcEntityPose localPose(double alpha) const;
    [[nodiscard]] std::optional<QcEntityMesher::ItemLook> heldLook(QcItemId item) const;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    QString m_forcedPose;
    // Cape swings of the drawn figures and the clock that advances them (decision E-240).
    QcCapeTracker m_capes;
    QcFovEffect m_fov;
    QElapsedTimer m_capeClock;
    QcPrecipitationField m_precipitation{QcPrecipitationField::selectedBudget()};
    // Lists of drops handed to the view: the view keeps the latest, the renderer the one it last
    // copied, so a third is always free to be filled again and a frame allocates nothing (E-397).
    std::array<std::shared_ptr<QList<QcPrecipitationDrop>>, 3> m_dropLists;
    // Vertex and index count of the last entity mesh.
    std::pair<qsizetype, qsizetype> m_entityMeshSize;
};
