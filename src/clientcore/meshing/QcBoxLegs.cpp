#include "clientcore/meshing/QcBoxLegs.h"

#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcMeshRotation.h"
#include "common/content/definitions/QcMobDefinition.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// How far above the lowest part a part may start and still stand on the ground, in metres.
constexpr double groundTolerance = 0.01;
// A leg is at least this many times as tall as it is wide and deep.
constexpr double legSlenderness = 1.5;
// A leg is taller than this, in metres (no swing for a box without height).
constexpr double minLegHeight = 0.05;
// A foot is at most this share of its leg's height.
constexpr double footFlatness = 0.25;

struct Leg
{
    int part = 0;
    QcVec3 centre;
};

bool standsOnGround(const QcMobPart &part, double ground)
{
    return part.from.y <= ground + groundTolerance;
}

bool isLeg(const QcMobPart &part, double ground)
{
    const double height = part.to.y - part.from.y;
    return standsOnGround(part, ground) && height > minLegHeight
           && height >= legSlenderness * std::max(part.to.x - part.from.x, part.to.z - part.from.z);
}

// The boxes overlap or touch.
bool touches(const QcMobPart &a, const QcMobPart &b)
{
    return a.from.x <= b.to.x && b.from.x <= a.to.x && a.from.y <= b.to.y && b.from.y <= a.to.y && a.from.z <= b.to.z
           && b.from.z <= a.to.z;
}

// `part` is a flat box touching `leg` (a chicken's foot): it moves with the leg.
bool isFootOf(const QcMobPart &part, const QcMobPart &leg)
{
    return touches(part, leg) && part.to.y - part.from.y <= footFlatness * (leg.to.y - leg.from.y);
}

// The lowest level of the model.
double groundOf(const QList<QcMobPart> &parts)
{
    double ground = std::numeric_limits<double>::max();
    for (const QcMobPart &part : parts) {
        ground = std::min(ground, part.from.y);
    }
    return ground;
}

QList<Leg> legsOf(const QList<QcMobPart> &parts, double ground)
{
    QList<Leg> legs;
    for (int index = 0; index < parts.size(); ++index) {
        const QcMobPart &part = parts.at(index);
        if (isLeg(part, ground)) {
            legs.append({index, (part.from + part.to) * 0.5});
        }
    }
    return legs;
}

// +1 or -1 for `leg`: the side, times the alternation of the legs along that side.
double phaseOf(const Leg &leg, const QList<Leg> &legs)
{
    const bool left = leg.centre.x < 0.0;
    int rank = 0;
    for (const Leg &other : legs) {
        if ((other.centre.x < 0.0) == left && other.centre.z < leg.centre.z) {
            ++rank;
        }
    }
    const double side = left ? 1.0 : -1.0;
    return rank % 2 == 0 ? side : -side;
}

// The widest swing at which two neighbouring legs of a side, turning towards each other, keep a
// tenth of the gap between them at their feet.
double maxSwingOf(const QList<Leg> &legs, const QList<QcMobPart> &parts)
{
    constexpr double keptGap = 0.9;
    double widest = QcAnimator::maxLegSwing;
    for (const Leg &front : legs) {
        for (const Leg &hind : legs) {
            const QcMobPart &a = parts.at(front.part);
            const QcMobPart &b = parts.at(hind.part);
            if ((front.centre.x < 0.0) != (hind.centre.x < 0.0) || b.from.z < a.to.z) {
                continue;
            }
            const double reach = (a.to.y - a.from.y) + (b.to.y - b.from.y);
            if (reach <= 0.0) {
                continue;
            }
            const double sine = std::clamp(keptGap * (b.from.z - a.to.z) / reach, 0.0, 1.0);
            widest = std::min(widest, qRadiansToDegrees(std::asin(sine)));
        }
    }
    return widest;
}

} // namespace

QcVec3 QcBoxLegs::Swing::apply(const QcVec3 &local, double degrees) const
{
    if (phase == 0.0 || degrees == 0.0) {
        return local;
    }
    const QcEntityMesher::Rotate turn(degrees * phase, 0.0);
    return hip + turn(local - hip);
}

QcBoxLegs QcBoxLegs::of(const QList<QcMobPart> &parts)
{
    const double ground = groundOf(parts);
    const QList<Leg> legs = legsOf(parts, ground);
    QcBoxLegs result;
    if (legs.size() < 2) {
        return result;
    }

    result.m_swings.resize(parts.size());
    for (const Leg &leg : legs) {
        const QcMobPart &part = parts.at(leg.part);
        result.m_swings[leg.part] = {{leg.centre.x, part.to.y, leg.centre.z}, phaseOf(leg, legs)};
        result.m_length += (part.to.y - part.from.y) / static_cast<double>(legs.size());
    }

    result.m_maxSwing = maxSwingOf(legs, parts);

    // Feet: flat parts on the ground that touch a leg follow it.
    for (int index = 0; index < parts.size(); ++index) {
        if (result.m_swings.at(index).phase != 0.0 || !standsOnGround(parts.at(index), ground)) {
            continue;
        }
        for (const Leg &leg : legs) {
            if (isFootOf(parts.at(index), parts.at(leg.part))) {
                result.m_swings[index] = result.m_swings.at(leg.part);
                break;
            }
        }
    }
    return result;
}

const QcBoxLegs &QcBoxLegsCache::of(const QList<QcMobPart> &parts)
{
    // Content reloads leave old lists behind; the cache starts over before it grows large.
    constexpr qsizetype maxEntries = 256;
    const auto found = m_entries.constFind(parts.constData());
    if (found != m_entries.constEnd()) {
        return found->legs;
    }
    if (m_entries.size() >= maxEntries) {
        m_entries.clear();
    }
    return m_entries.insert(parts.constData(), {parts, QcBoxLegs::of(parts)})->legs;
}
