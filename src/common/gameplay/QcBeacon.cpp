// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcBeacon.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The effects a beacon gives and the level from which each may be chosen, in the order of the
// screen (the reference's BeaconBlockEntity.BEACON_EFFECTS).
constexpr std::array<std::pair<QLatin1StringView, int>, 6> tiers{{
    {QLatin1StringView("qtcraft:speed"), 1},
    {QLatin1StringView("qtcraft:haste"), 1},
    {QLatin1StringView("qtcraft:resistance"), 2},
    {QLatin1StringView("qtcraft:jump_boost"), 2},
    {QLatin1StringView("qtcraft:strength"), 3},
    {QLatin1StringView("qtcraft:regeneration"), 4},
}};

// The half of each channel of two colours, as the reference mixes a section with the glass above.
quint32 mixed(quint32 a, quint32 b)
{
    quint32 result = 0;
    for (const int shift : {16, 8, 0}) {
        const quint32 channel = (((a >> shift) & 0xFFu) + ((b >> shift) & 0xFFu)) / 2;
        result |= channel << shift;
    }
    return result;
}

// Whether every block of the square of side 2n + 1 at `y` around `beacon` is a base block.
bool layerIsFull(const QcContentRegistry &content, QcBlockPos beacon, int n, const QcBeacon::BlockAt &blockAt)
{
    for (int dx = -n; dx <= n; ++dx) {
        for (int dz = -n; dz <= n; ++dz) {
            const std::optional<QcBlockId> block = blockAt(beacon.offset(dx, -n, dz));
            if (!block || !content.isValidBlock(*block) || !content.block(*block).itemTags.contains(QcBeacon::baseTag)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace QcBeacon {

QStringList primaryEffects(int level)
{
    QStringList effects;
    for (const auto &[effect, from] : tiers) {
        if (from <= std::min(level, secondaryLevel - 1)) {
            effects.append(effect);
        }
    }
    return effects;
}

QString secondaryEffect()
{
    return tiers.back().first;
}

int levelOf(const QString &effect)
{
    for (const auto &[name, from] : tiers) {
        if (effect == name) {
            return from;
        }
    }
    return 0;
}

bool mayChoose(int level, const QString &primary, const QString &secondary)
{
    if (level < 1 || !primaryEffects(level).contains(primary)) {
        return false;
    }
    return secondary.isEmpty() || (level >= secondaryLevel && (secondary == primary || secondary == secondaryEffect()));
}

bool reaches(QcBlockPos beacon, int level, const QcAabb &body)
{
    // The reference's box: the beacon's cell grown by the range on every side, then by the world's
    // height upwards; the player's box only needs to touch it.
    const double range = rangeOf(level);
    const bool across = body.max.x >= beacon.x - range && body.min.x <= beacon.x + 1 + range && body.max.z >= beacon.z - range
                        && body.min.z <= beacon.z + 1 + range;
    return across && body.max.y >= beacon.y - range;
}

QList<QcStatusEffect> effectsOf(int level, const QString &primary, const QString &secondary)
{
    QList<QcStatusEffect> effects;
    if (level < 1 || primary.isEmpty()) {
        return effects;
    }
    const int ticks = durationTicksOf(level);
    const bool strong = level >= secondaryLevel && secondary == primary;
    effects.append({primary, strong ? 2 : 1, ticks});
    if (level >= secondaryLevel && !secondary.isEmpty() && secondary != primary) {
        effects.append({secondary, 1, ticks});
    }
    return effects;
}

bool isPayment(const QcContentRegistry &content, QcItemId item)
{
    return content.isValidItem(item) && content.item(item).tags.contains(paymentTag);
}

int levelAt(const QcContentRegistry &content, QcBlockPos beacon, const BlockAt &blockAt)
{
    int level = 0;
    for (int n = 1; n <= maxLevel && beacon.y - n >= 0; ++n) {
        if (!layerIsFull(content, beacon, n, blockAt)) {
            break;
        }
        level = n;
    }
    return level;
}

QList<QcBeamSection> beamAt(const QcContentRegistry &content, QcBlockPos beacon, const BlockAt &blockAt)
{
    // The beacon itself starts the first section in its own plain colour.
    QList<QcBeamSection> sections{{plainColour, 1}};
    for (int y = beacon.y + 1; y < QcWorld::worldHeight; ++y) {
        const std::optional<QcBlockId> block = blockAt({beacon.x, y, beacon.z});
        if (!block || !content.isValidBlock(*block)) {
            return {};
        }

        const QcBlockDefinition &definition = content.block(*block);
        if (const std::optional<quint32> colour = definition.beamColour) {
            if (sections.size() == 1) {
                sections.append({*colour, 1});
            } else if (sections.back().colour == *colour) {
                ++sections.back().height;
            } else {
                sections.append({mixed(sections.back().colour, *colour), 1});
            }
            continue;
        }
        if (content.state(*block).lightOpacity >= 15 && definition.name != beamPassesBlock) {
            return {};
        }
        ++sections.back().height;
    }
    return sections;
}

} // namespace QcBeacon
