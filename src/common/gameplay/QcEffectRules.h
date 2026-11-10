// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcStatusEffects.h"
#include "common/physics/QcPlayerInput.h"

/*! \brief What the status effects of a body add up to (decision E-992), from the fields of their
    kinds (QcEffectDefinition): one place for the server, the client's prediction and the renderer.

    Every function takes the kinds from a content registry and the effects of a body; kinds no pack
    has do nothing, and a body without effects gets the value of an unaffected body. */
namespace QcEffectRules {

/*! The highest factor on the velocity of a jump: 16 times the height of a plain jump, so that no
    mod's level sends a player out of the world. */
inline constexpr double maxJumpFactor = 4.0;

/*! Returns what \a effects of kinds of \a content do to the steps of a player: speed, jump
    (within 0 and maxJumpFactor) and fall. */
[[nodiscard]] QcMovementEffects movement(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns the blocks a fall of a body with \a effects of kinds of \a content hurts later (one per
    level of jump boost). */
[[nodiscard]] int fallTolerance(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns the points \a effects of kinds of \a content add to a blow, negative when they weaken
    it. */
[[nodiscard]] double attackBonus(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns the share of a hit that \a effects of kinds of \a content let through, in [0, 1]. */
[[nodiscard]] double damageShare(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns whether fire, lava and hot blocks leave a body with \a effects of kinds of \a content
    unharmed. */
[[nodiscard]] bool fireProof(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns whether a player with \a effects of kinds of \a content keeps his breath under water. */
[[nodiscard]] bool breathesUnderWater(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns whether a player with \a effects of kinds of \a content sees the world bright. */
[[nodiscard]] bool nightVision(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns whether a body with \a effects of kinds of \a content is invisible. */
[[nodiscard]] bool invisible(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! Returns the share of creatures' sight range at which they notice a body with \a effects of
    kinds of \a content that wears \a armourPieces of four armour pieces: 1 for a visible one. */
[[nodiscard]] double sightShare(const QcContentRegistry &content, const QList<QcStatusEffect> &effects, int armourPieces);
/*! Returns the factor on the mining speed of a body with \a effects of kinds of \a content, above
    0. */
[[nodiscard]] double miningFactor(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
/*! The most extra health a player can have: ten absorbing effects of the highest amount. The
    wire refuses more. */
inline constexpr double maxAbsorption = 10.0 * 1000.0;

/*! Returns the most extra health \a effects of kinds of \a content give a player, at most
    maxAbsorption. */
[[nodiscard]] double absorptionLimit(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);

} // namespace QcEffectRules
