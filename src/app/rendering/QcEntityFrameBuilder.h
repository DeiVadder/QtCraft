#pragma once

#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/maps/QcFramedMapAtlas.h"
#include "clientcore/meshing/QcCapeTracker.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/meshing/QcSignTextCache.h"
#include "clientcore/meshing/QcSpriteShapes.h"
#include "clientcore/session/QcCampfireSmoke.h"
#include "common/core/QcBlockPos.h"
#include "common/physics/QcPhysics.h"

#include <QElapsedTimer>
#include <QString>

#include <memory>
#include <optional>
#include <utility>

struct QcClientEntity;
class QcClientWorld;
class QcContentRegistry;
class QcSettingsController;
class QcTextureAtlas;
struct QcSessionParts;

// Builds the entity meshes of one frame from the running session: creatures, armour stands, carts
// with their load, drops, projectiles, flames, lit explosives and the puffs of explosions, the local
// player seen from outside and the
// first-person arm, with their capes and the light they stand in. Hands nothing to the view; that
// is QcRenderBridge's part. Lives in the GUI thread.
class QcEntityFrameBuilder
{
public:
    QcEntityFrameBuilder(const QcSessionParts &parts, const QcSettingsController &settings);

    // Texture atlas of the running world's content; without one no entities are built. A new atlas
    // drops the sign texts cut from the old one.
    void setAtlas(std::shared_ptr<const QcTextureAtlas> atlas)
    {
        m_atlas = std::move(atlas);
        m_signTexts.clear();
        m_mapColoursStale = true;
    }
    // Test hook: freezes the local player's pose ("walk", "sprint", "swing" or "swing:<progress>", "swim", "jump",
    // "fall", "sneak", "death") for screenshots; the cape then rests on its target for that movement.
    void setForcedPose(const QString &pose) { m_forcedPose = pose; }

    // The local player's pose at `alpha` between the last two ticks, or the forced pose.
    QcEntityPose localPose(double alpha) const;
    // Creatures, dropped items, falling blocks and, seen from outside, the player; null without an
    // atlas. The maps in item frames go into a mesh of their own (framedMaps()).
    [[nodiscard]] std::shared_ptr<QcEntityMesh> buildEntities(double alpha, QcCameraMode mode);
    // The maps in item frames of the last buildEntities, drawn with the framed-map texture
    // (decision E-809); null before the first.
    [[nodiscard]] std::shared_ptr<QcEntityMesh> framedMaps() const { return m_framedMapMesh; }
    // The tiles of the framed-map texture the framed maps' coordinates point into.
    [[nodiscard]] std::shared_ptr<const QList<QcFramedMapAtlas::Tile>> framedMapTiles() const { return m_framedMaps.tiles(); }
    // The first-person arm with the held item at `eye`; null without an atlas or while dead.
    [[nodiscard]] std::shared_ptr<QcEntityMesh> buildHand(double alpha, const QcVec3 &eye) const;

private:
    // A lit explosive flashes every this many ticks and swells in its last ticks (decision E-604).
    static constexpr int primedFlashTicks = 5;
    static constexpr int primedSwellTicks = 10;

    // What every entity of one frame is drawn with.
    struct EntityFrame
    {
        const QcContentRegistry &content;
        const QcClientWorld &world;
        const QcEntityMesher::UvLookup &uv;
        double alpha = 0.0;
        double seconds = 0.0;
    };

    // A creature; `scale` shrinks it about its feet (the small creature in a spawner).
    void appendCreature(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame, double scale = 1.0);
    // The small creature spinning in each monster spawner near the player (decision E-520).
    void appendSpawners(QcEntityMesh &mesh, const EntityFrame &frame);
    // The text on the signs near the player (decision E-766).
    void appendSigns(QcEntityMesh &mesh, const EntityFrame &frame);
    // The items lying on the campfires near the player and the smoke rising from them (decision E-772).
    void appendCampfires(QcEntityMesh &mesh, const EntityFrame &frame);
    // The maps in item frames (decision E-806), their tiles given nearest to the camera first
    // (decision E-809).
    [[nodiscard]] std::shared_ptr<QcEntityMesh> buildFramedMaps(const EntityFrame &frame);
    // A falling or moving block or a lit explosive, drawn as its block.
    void appendBlockEntity(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame);
    // The puffs of flash and smoke of explosions at the client tick `ticks` (with its fraction).
    void appendExplosions(QcEntityMesh &mesh, const EntityFrame &frame, double ticks);
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
    // What a hand holds: a sprite with its shape (decision E-831).
    [[nodiscard]] std::optional<QcEntityMesher::ItemLook> heldLook(QcItemId item) const;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    // A cache: the shapes of held sprites, cut from the atlas on first use (decision E-831). GUI
    // thread only: frames are built there, and the cache is not locked.
    mutable QcSpriteShapes m_spriteShapes;
    QString m_forcedPose;
    // Cape swings of the drawn figures and the clock that advances them (decision E-240).
    QcCapeTracker m_capes;
    // Text quads of the signs, rebuilt only when a sign changes (decision E-766).
    QcSignTextCache m_signTexts;
    QcCampfireSmoke m_smoke;
    // The pictures of the framed maps, kept from frame to frame, and the mesh of their quads.
    QcFramedMapAtlas m_framedMaps;
    std::shared_ptr<QcEntityMesh> m_framedMapMesh;
    // The map colours come from the atlas; true until worked out for the current one.
    bool m_mapColoursStale = true;
    QElapsedTimer m_capeClock;
    // Vertex and index count of the last entity mesh.
    std::pair<qsizetype, qsizetype> m_entityMeshSize;
};
