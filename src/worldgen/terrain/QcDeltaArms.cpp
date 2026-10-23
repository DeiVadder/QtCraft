#include "worldgen/terrain/QcDeltaArms.h"

#include "worldgen/terrain/QcTerrainMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using QcTerrainMath::smoothstep;

namespace {

constexpr double tau = 2.0 * std::numbers::pi;

// Where the river first branches (along the fan) and how far its first arms stray to either side
// (shares of the fan's half width); the third, middle arm is there for some deltas only.
constexpr double firstSplit = 0.1;
constexpr double firstSplitSpread = 0.12;
constexpr double firstOffset = 0.4;
constexpr double firstOffsetSpread = 0.2;
constexpr double middleOffset = 0.12;
// Branches of the first arms: where they leave and how far they stray; their own small branches
// near the coast.
constexpr double branchFrom = 0.36;
constexpr double branchSpread = 0.22;
constexpr double branchOffset = 0.18;
constexpr double branchOffsetSpread = 0.12;
constexpr double twigFrom = 0.76;
constexpr double twigSpread = 0.14;
constexpr double twigOffset = 0.13;
// Side arms that leave a first arm and join it again around an island.
constexpr double loopFrom = 0.22;
constexpr double loopSpread = 0.16;
constexpr double loopLength = 0.18;
constexpr double loopOffset = 0.1;
// Half the width of an arm by its level, shares of half the river's width (never below
// narrowest blocks while it is there); towards the sea up to `narrowing` narrower.
constexpr double firstWidth = 0.85;
constexpr double branchWidth = 0.6;
constexpr double twigWidth = 0.42;
constexpr double loopWidth = 0.36;
constexpr double narrowest = 1.1;
constexpr double narrowing = 0.3;
// The arms stay within this share of the fan's half width.
constexpr double armSide = 0.86;
// Depth of an arm at its middle (base plus a share of its half width, up to `shallower` less towards
// the sea); its banks rise this much per block.
constexpr double armDepth = 0.85;
constexpr double armDepthPerWidth = 0.35;
constexpr double shallower = 0.3;
constexpr double armBank = 0.5;

} // namespace

QcDeltaArms::QcDeltaArms(const QcDeltaSites::Site &site)
    : m_site(site)
{
    const double split = firstSplit + firstSplitSpread * nextTrait();
    // The river itself, up to a little past its first branching.
    add({.start = -1.0, .ramp = 0.1, .until = split + 0.08, .width = std::max(site.riverHalfWidth, narrowest)});
    addFirstLevel(split, site.riverHalfWidth);
}

double QcDeltaArms::nextTrait()
{
    return m_site.traits[std::size_t(m_trait++ % QcDeltaSites::traitCount)];
}

int QcDeltaArms::add(Arm arm)
{
    // The network has a fixed number of arms (maxArms is never reached), an arm whose parent is
    // missing hangs on the river.
    if (m_count >= maxArms) {
        return 0;
    }
    arm.wave = 2.0 + 4.5 * nextTrait();
    arm.frequency = 0.8 + 1.2 * nextTrait();
    arm.phase = nextTrait();
    m_arms[std::size_t(m_count)] = arm;
    return m_count++;
}

void QcDeltaArms::addFirstLevel(double split, double riverWidth)
{
    // Left and right, offsets a little different; a middle arm where the traits say so (its width
    // fades in, so no delta jumps between two and three arms).
    const double left = -(firstOffset + firstOffsetSpread * nextTrait());
    const double right = firstOffset + firstOffsetSpread * nextTrait();
    const double middle = middleOffset * (2.0 * nextTrait() - 1.0);
    const double withMiddle = smoothstep(0.4, 0.6, nextTrait());
    for (const auto [side, share] : {std::pair{left, 1.0}, std::pair{right, 1.0}, std::pair{middle, withMiddle}}) {
        const double width = firstWidth * riverWidth * share;
        const int arm = add({.parent = 0, .start = split, .ramp = 0.25 + 0.15 * nextTrait(), .offset = side, .width = width});
        // Every arm takes its traits whether it is there or not: the order of the traits never
        // depends on a threshold (columns of a delta might read traits a hair apart).
        addBranches(arm, side, riverWidth * share);
    }
}

void QcDeltaArms::addBranches(int parent, double side, double riverWidth)
{
    // One branch outwards and, now and then, one inwards; each may split once more near the coast.
    const double outwards = side >= 0.0 ? 1.0 : -1.0;
    for (const double direction : {outwards, -outwards}) {
        const double there = smoothstep(direction == outwards ? 0.15 : 0.5, direction == outwards ? 0.35 : 0.7, nextTrait());
        const double start = branchFrom + branchSpread * nextTrait() + (direction == outwards ? 0.0 : 0.12);
        const double offset = direction * (branchOffset + branchOffsetSpread * nextTrait());
        const int branch = add({.parent = parent, .start = start, .ramp = 0.22, .offset = offset, .width = branchWidth * riverWidth * there});
        const double twig = smoothstep(0.45, 0.65, nextTrait());
        const double twigStart = twigFrom + twigSpread * nextTrait();
        const double twigSide = twigOffset * (2.0 * nextTrait() - 1.0);
        add({.parent = branch, .start = twigStart, .ramp = 0.15, .offset = twigSide, .width = twigWidth * riverWidth * twig * there});
    }
    // A side arm around an island, bulging outwards.
    const double loop = smoothstep(0.5, 0.7, nextTrait());
    const double start = loopFrom + loopSpread * nextTrait();
    add({.parent = parent,
         .start = start,
         .offset = outwards * loopOffset * (1.0 + 0.6 * nextTrait()),
         .rejoin = start + loopLength * (1.0 + 0.6 * nextTrait()),
         .width = loopWidth * riverWidth * loop});
}

double QcDeltaArms::middleOf(const Arm &arm, double parentMiddle, double along, double halfWidth) const
{
    // Away from its parent along a smooth bend (or a bulge out and back for a side arm), meandering
    // once away from the parent.
    double away = 0.0;
    double strayed = 1.0;
    if (arm.rejoin > 0.0) {
        strayed = std::sin(std::numbers::pi * std::clamp((along - arm.start) / (arm.rejoin - arm.start), 0.0, 1.0));
    } else if (arm.parent >= 0) {
        strayed = smoothstep(arm.start, arm.start + arm.ramp, along);
    }
    away = arm.offset * halfWidth * strayed;
    const double bends = arm.wave * strayed * (0.65 * std::sin(tau * (along * arm.frequency + arm.phase))
                                               + 0.35 * std::sin(tau * (along * 2.3 * arm.frequency + 0.37 + arm.phase)));
    return parentMiddle + away + bends;
}

double QcDeltaArms::widthOf(const Arm &arm, double along)
{
    if (along < arm.start || (arm.rejoin > 0.0 && along > arm.rejoin) || along > arm.until) {
        return 0.0;
    }
    // Narrow where it leaves its parent, narrower towards the sea, wider and narrower on the way.
    const double leaving = arm.parent >= 0 ? 0.55 + 0.45 * smoothstep(arm.start, arm.start + 0.1, along) : 1.0;
    const double ending = arm.rejoin > 0.0 ? std::sin(std::numbers::pi * (along - arm.start) / (arm.rejoin - arm.start)) : 1.0;
    const double varying = 0.85 + 0.15 * std::sin(tau * (along * 2.7 + arm.phase * 1.7));
    const double width = arm.width * leaving * varying * (1.0 - narrowing * smoothstep(0.55, 1.25, along))
                         * (1.0 - smoothstep(arm.until - 0.08, arm.until, along)) * std::min(1.0, 0.4 + ending);
    return width >= 0.6 ? std::max(width, narrowest) * std::min(1.0, width / narrowest) : 0.0;
}

double QcDeltaArms::channelAt(double along, double lateral, double halfWidth, double edge) const
{
    // The lowest of all arms' grounds: under an arm down to its bed, rising over its banks.
    std::array<double, maxArms> middles{};
    const double side = std::max(1.0, armSide * halfWidth);
    double channel = edge + armBank * 64.0;
    for (int i = 0; i < m_count; ++i) {
        const Arm &arm = m_arms[std::size_t(i)];
        const double parent = arm.parent >= 0 ? middles[std::size_t(arm.parent)] : 0.0;
        middles[std::size_t(i)] = middleOf(arm, parent, along, halfWidth);
        const double width = widthOf(arm, along);
        if (width <= 0.0) {
            continue;
        }
        const double middle = side * std::tanh(middles[std::size_t(i)] / side);
        const double distance = std::abs(lateral - middle);
        double ground = edge + armBank * (distance - width);
        if (distance < width) {
            const double share = distance / width;
            const double depth = (armDepth + armDepthPerWidth * width) * (1.0 - shallower * smoothstep(0.7, 1.25, along));
            ground = edge - depth * (1.0 - share * share);
        }
        channel = std::min(channel, ground);
    }
    return channel;
}
