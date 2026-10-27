#pragma once

#include "clientcore/settings/QcRenderOptions.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcGameMode.h"
#include "common/gameplay/QcItemStack.h"

#include <QHash>
#include <QList>

#include <array>
#include <optional>

class QcClientSession;
struct QcClientEntity;
class QcContentRegistry;
struct QcMobDefinition;

// A light that moves with what carries it (decision E-902): a torch in a hand, a glowing item on
// the ground, a firefly. Only the realistic mode draws it, in the shaders; the block light of the
// world, the server and every rule of the game stay as they are.
struct QcMovingLight
{
    // The colour of the light: warm like the block light of torches, or the yellow-green glow of
    // creatures (fireflies).
    enum class Colour : quint8 { Warm, Glow };

    QcVec3 position;
    // Like a block light level: 0..15, one level less per block of distance.
    float level = 0.0f;
    Colour colour = Colour::Warm;

    friend bool operator==(const QcMovingLight &, const QcMovingLight &) = default;
};

// The moving lights as the shaders take them (the frame uniforms, decision E-902).
struct QcPackedLights
{
    // xyz relative to the camera, w the level (0..15, below 16) plus 16 times the colour.
    std::array<std::array<float, 4>, QcRenderOptions::maxMovingLights> lights{};
    int count = 0;
};

// Finds and ranks the moving lights of a frame. GUI thread; the helpers are pure.
namespace QcMovingLights {

// A glowing creature lights its surroundings with this share of its glow: a firefly (glow 15)
// lights about seven blocks around it, a little light rather than a torch.
inline constexpr float creatureGlowShare = 0.5f;
// Lights farther from the camera than this many blocks beyond their reach are not drawn.
inline constexpr double farthest = 48.0;
// Where a torch is held below the eyes, and where a glowing item lying on the ground shines from.
inline constexpr double heldBelowEyes = 0.3;
inline constexpr double itemCentre = 0.25;
// A creature holds an item at this share of its height and glows from this share.
inline constexpr double creatureHeldHeight = 0.6;
inline constexpr double creatureGlowHeight = 0.5;

// The light level (0..15) an item gives when held or lying: the block light of the block it
// places (a torch 14, a lantern 15, a bucket of lava 15); 0 for the others.
[[nodiscard]] float itemLevel(const QcContentRegistry &content, QcItemId item);
// The light level a creature gives: creatureGlowShare of its glow.
[[nodiscard]] float creatureLevel(const QcMobDefinition &mob);
// Brightness (0..1) of a light of `level` at `distance` blocks: like block light, one level less
// per block, a level of 15 is full brightness. The shaders compute it themselves (block.frag,
// entity.vert, hand.vert); this CPU copy states the rule for the tests. The distance is straight
// (Euclidean), so the light reaches a little farther diagonally than block light does.
[[nodiscard]] float brightness(float level, double distance);
// The lights a frame draws, at most `count`: those that bring the most light to the camera first
// (the level less the distance), ties by position so the choice does not flicker between frames
// of the same scene; lights farther than their level plus `farthest` are left out.
[[nodiscard]] QList<QcMovingLight> select(const QList<QcMovingLight> &candidates, const QcVec3 &camera, int count);
// The first `most` of `lights` (at most QcRenderOptions::maxMovingLights) for the shaders, relative
// to `camera`; lights with a position or level that is not finite are left out.
[[nodiscard]] QcPackedLights pack(const QList<QcMovingLight> &lights, const QcVec3 &camera, int most);
// Whether the local player holds anything in the world: not when dead or a spectator.
[[nodiscard]] bool handInWorld(bool dead, QcGameModes::Rules rules);
// The light of the local player's `held` stack, heldBelowEyes below `eye`; none for items
// without light.
[[nodiscard]] std::optional<QcMovingLight> handLight(const QcContentRegistry &content, const QcItemStack &held, const QcVec3 &eye);
// The lights of the mirrored entities at `alpha` between the last two ticks: what players hold
// (heldBelowEyes below their eyes) and creatures hold (at creatureHeldHeight), glowing creatures
// (at creatureGlowHeight) and glowing items on the ground (itemCentre above them).
[[nodiscard]] QList<QcMovingLight> entityLights(const QcContentRegistry &content, const QHash<quint32, QcClientEntity> &entities,
                                                double alpha);
// Every moving light of the session: handLight() when handInWorld(), then entityLights().
[[nodiscard]] QList<QcMovingLight> gather(const QcClientSession &session, double alpha, const QcVec3 &eye);

} // namespace QcMovingLights
