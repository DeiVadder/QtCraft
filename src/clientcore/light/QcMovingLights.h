// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcRenderOptions.h"
#include "common/content/items/QcItemDefinition.h"
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

/*! \brief A light that moves with what carries it (decision E-902): a torch in a hand, a glowing
    item on the ground, a firefly.

    Only the realistic mode draws it, in the shaders; the block light of the world, the server
    and every rule of the game stay as they are. */
struct QcMovingLight
{
    /*! \brief The colour of the light: warm like the block light of torches, or the yellow-green
        glow of creatures (fireflies). */
    enum class Colour : quint8 {
        Warm, // Like the block light of torches
        Glow  // The yellow-green glow of creatures
    };

    /*! Where the light shines from. */
    QcVec3 position;
    /*! Like a block light level: 0..15, one level less per block of distance. */
    float level = 0.0f;
    /*! Colour of the light. */
    Colour colour = Colour::Warm;

    friend bool operator==(const QcMovingLight &, const QcMovingLight &) = default;
};

/*! \brief The moving lights as the shaders take them (the frame uniforms, decision E-902). */
struct QcPackedLights
{
    /*! xyz relative to the camera, w the level (0..15, below 16) plus 16 times the colour. */
    std::array<std::array<float, 4>, QcRenderOptions::maxMovingLights> lights{};
    /*! Number of lights used in \c {lights}. */
    int count = 0;
};

/*! \brief Finds and ranks the moving lights of a frame.

    GUI thread; the helpers are pure. */
namespace QcMovingLights {

/*! A glowing creature lights its surroundings with this share of its glow: a firefly (glow 15)
    lights about seven blocks around it, a little light rather than a torch. */
inline constexpr float creatureGlowShare = 0.5f;
/*! Lights farther from the camera than this many blocks beyond their reach are not drawn. */
inline constexpr double farthest = 48.0;
/*! Where a torch is held below the eyes, and where a glowing item lying on the ground shines
    from. \qcshared */
inline constexpr double heldBelowEyes = 0.3;
inline constexpr double itemCentre = 0.25;
/*! A creature holds an item at this share of its height and glows from this share. \qcshared */
inline constexpr double creatureHeldHeight = 0.6;
inline constexpr double creatureGlowHeight = 0.5;

/*! Returns the light level (0..15) the \a item of \a content gives when held or lying: the block
    light of the block it places (a torch 14, a lantern 15, a bucket of lava 15); 0 for the
    others. */
[[nodiscard]] float itemLevel(const QcContentRegistry &content, QcItemId item);
/*! Returns the light level the creature \a mob gives: creatureGlowShare of its glow. */
[[nodiscard]] float creatureLevel(const QcMobDefinition &mob);
/*! Returns the brightness (0..1) of a light of \a level at \a distance blocks: like block light,
    one level less per block, a level of 15 is full brightness. The shaders compute it themselves
    (block.frag, entity.vert, hand.vert); this CPU copy states the rule for the tests. The
    distance is straight (Euclidean), so the light reaches a little farther diagonally than block
    light does. */
[[nodiscard]] float brightness(float level, double distance);
/*! Returns the lights of \a candidates a frame draws, at most \a count: those that bring the most
    light to \a camera first (the level less the distance), ties by position so the choice does not
    flicker between frames of the same scene; lights farther than their level plus \c {farthest} are
    left out. */
[[nodiscard]] QList<QcMovingLight> select(const QList<QcMovingLight> &candidates, const QcVec3 &camera, int count);
/*! Returns the first \a most of \a lights (at most QcRenderOptions::maxMovingLights) for the
    shaders, relative to \a camera; lights with a position or level that is not finite are left
    out. */
[[nodiscard]] QcPackedLights pack(const QList<QcMovingLight> &lights, const QcVec3 &camera, int most);
/*! Returns whether the local player holds anything in the world: not when \a dead or when
    \a rules make them a spectator. */
[[nodiscard]] bool handInWorld(bool dead, QcGameModes::Rules rules);
/*! Returns the light of the local player's \a held stack, heldBelowEyes below \a eye; none for
    items without light in \a content. */
[[nodiscard]] std::optional<QcMovingLight> handLight(const QcContentRegistry &content, const QcItemStack &held, const QcVec3 &eye);
/*! Returns the lights of the mirrored \a entities of \a content at \a alpha between the last two
    ticks: what players hold (heldBelowEyes below their eyes) and creatures hold (at
    creatureHeldHeight), glowing creatures (at creatureGlowHeight) and glowing items on the
    ground (itemCentre above them). */
[[nodiscard]] QList<QcMovingLight> entityLights(const QcContentRegistry &content, const QHash<quint32, QcClientEntity> &entities,
                                                double alpha);
/*! Returns every moving light of \a session at \a alpha between the last two ticks, the local
    player's eyes at \a eye: handLight() when handInWorld(), then entityLights(). */
[[nodiscard]] QList<QcMovingLight> gather(const QcClientSession &session, double alpha, const QcVec3 &eye);

} // namespace QcMovingLights
