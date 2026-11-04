// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcEffectRules.h"

#include "common/content/items/QcEffectDefinition.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

namespace {

// The least of the mining speed that a tiring effect leaves.
constexpr double slowestMining = 0.01;
// The share of the armour's cover that counts at least, like the reference.
constexpr double leastCover = 0.1;
constexpr int armourSlots = 4;

// Calls `visit` with the kind and the level of each of `effects` a pack knows.
template<typename Visit>
void forEachKind(const QcContentRegistry &content, const QList<QcStatusEffect> &effects, Visit visit)
{
    for (const QcStatusEffect &effect : effects) {
        if (const QcEffectDefinition *kind = content.effect(effect.name)) {
            visit(*kind, effect.level);
        }
    }
}

// Returns the sum of `share` of the kinds of `effects` times their levels.
template<typename Share>
double levelSum(const QcContentRegistry &content, const QList<QcStatusEffect> &effects, Share share)
{
    double sum = 0.0;
    forEachKind(content, effects, [&](const QcEffectDefinition &kind, int level) { sum += share(kind) * level; });
    return sum;
}

// Returns whether a kind of `effects` has `flag`.
bool anyKind(const QcContentRegistry &content, const QList<QcStatusEffect> &effects, bool QcEffectDefinition::*flag)
{
    bool found = false;
    forEachKind(content, effects, [&](const QcEffectDefinition &kind, int) { found = found || kind.*flag; });
    return found;
}

// Returns the product of the visibility of the kinds of `effects`.
double visibilityOf(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    double visibility = 1.0;
    forEachKind(content, effects, [&](const QcEffectDefinition &kind, int) { visibility *= kind.visibility; });
    return visibility;
}

} // namespace

namespace QcEffectRules {

QcMovementEffects movement(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    QcMovementEffects moves;
    moves.speed = static_cast<float>(QcStatusEffects::speedFactor(content, effects));
    const double jump = 1.0 + levelSum(content, effects, [](const QcEffectDefinition &kind) { return kind.jump; });
    moves.jump = static_cast<float>(std::clamp(jump, 0.0, maxJumpFactor));
    forEachKind(content, effects, [&moves](const QcEffectDefinition &kind, int) {
        const float limit = static_cast<float>(kind.maxFallSpeed);
        if (limit > 0.0f) {
            moves.maxFallSpeed = moves.maxFallSpeed > 0.0f ? std::min(moves.maxFallSpeed, limit) : limit;
        }
    });
    return moves;
}

int fallTolerance(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    int blocks = 0;
    forEachKind(content, effects, [&blocks](const QcEffectDefinition &kind, int level) { blocks += kind.jump > 0.0 ? level : 0; });
    return blocks;
}

double attackBonus(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return levelSum(content, effects, [](const QcEffectDefinition &kind) { return kind.attack; });
}

double damageShare(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return std::clamp(1.0 - levelSum(content, effects, [](const QcEffectDefinition &kind) { return kind.resistance; }), 0.0, 1.0);
}

bool fireProof(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return anyKind(content, effects, &QcEffectDefinition::fireProof);
}

bool breathesUnderWater(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return anyKind(content, effects, &QcEffectDefinition::waterBreathing);
}

bool nightVision(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return anyKind(content, effects, &QcEffectDefinition::nightVision);
}

bool invisible(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return visibilityOf(content, effects) < 1.0;
}

double sightShare(const QcContentRegistry &content, const QList<QcStatusEffect> &effects, int armourPieces)
{
    const double visibility = visibilityOf(content, effects);
    if (visibility >= 1.0) {
        return 1.0;
    }
    return visibility * std::max(leastCover, static_cast<double>(armourPieces) / armourSlots);
}

double miningFactor(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return std::max(slowestMining, 1.0 + levelSum(content, effects, [](const QcEffectDefinition &kind) { return kind.mining; }));
}

double absorptionLimit(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    return std::min(levelSum(content, effects, [](const QcEffectDefinition &kind) { return kind.absorption; }), maxAbsorption);
}

} // namespace QcEffectRules
