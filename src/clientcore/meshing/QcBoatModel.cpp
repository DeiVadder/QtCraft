// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcBoatModel.h"

#include <QtMath>

#include <cmath>
#include <numbers>

namespace QcBoatModel {

namespace {

// The oarlock on the right wall, beside the driver's knees (the driver sits 0.3 m behind the middle,
// QcBoatBody::driverAlong): the paddle turns about it.
constexpr QcVec3 rightOarlock{0.66, wallHeight + 0.03, 0.0};
// Where the hand holds the paddle: the inner end of the shaft.
constexpr double gripAt = -0.4;

} // namespace

QList<QcMobPart> parts(const QString &planks)
{
    // The hull: a floor, two sides, bow (ahead, -z) and stern; the walls rise to wallHeight, the
    // floor (QcBoatBody::floor) carries the riders; an oarlock pin on top of each side wall.
    return {
        {{-0.55, 0.0, -0.8}, {0.55, 0.09, 0.8}, planks},
        {{-0.64, 0.0, -0.8}, {-0.55, wallHeight, 0.8}, planks},
        {{0.55, 0.0, -0.8}, {0.64, wallHeight, 0.8}, planks},
        {{-0.55, 0.06, -0.92}, {0.55, wallHeight, -0.8}, planks},
        {{-0.55, 0.06, 0.8}, {0.55, wallHeight, 0.9}, planks},
        {{-0.69, wallHeight - 0.04, -0.05}, {-0.62, wallHeight + 0.04, 0.05}, planks},
        {{0.62, wallHeight - 0.04, -0.05}, {0.69, wallHeight + 0.04, 0.05}, planks},
    };
}

QList<QcMobPart> paddleParts(const QString &planks, int side)
{
    // The shaft from the grip inside the boat through the oarlock, the blade at its outer end, flat
    // across the boat so it pushes the water back.
    QList<QcMobPart> parts{
        {{gripAt - 0.06, -0.035, -0.035}, {gripAt + 0.06, 0.035, 0.035}, planks},
        {{gripAt + 0.06, -0.025, -0.025}, {0.95, 0.025, 0.025}, planks},
        {{0.62, -0.12, -0.02}, {1.05, 0.12, 0.02}, planks},
    };
    if (side < 0) {
        for (QcMobPart &part : parts) {
            part = {{-part.to.x, part.from.y, part.from.z}, {-part.from.x, part.to.y, part.to.z}, part.texture};
        }
    }
    return parts;
}

Paddle paddleAt(double rowPhase)
{
    if (rowPhase < 0.0) {
        // At rest along the side, the blade towards the stern and in the water.
        return {60.0, 36.0};
    }
    // From the bow to the stern with the blade deep in the water (the stroke), back over it with
    // the blade lifted (the recovery).
    const double angle = 2.0 * std::numbers::pi * rowPhase;
    return {7.5 - 37.5 * std::cos(angle), 26.0 + 14.0 * std::sin(angle)};
}

QcVec3 paddlePoint(int side, Paddle paddle, const QcVec3 &local)
{
    // The right paddle tilts about the boat's length (outer end down), then turns about the vertical
    // (outer end towards the stern); the left one is its mirror image.
    const double lift = qDegreesToRadians(paddle.lift);
    const double sweep = qDegreesToRadians(paddle.sweep);
    const QcVec3 tilted{local.x * std::cos(lift) + local.y * std::sin(lift), -local.x * std::sin(lift) + local.y * std::cos(lift), local.z};
    const QcVec3 turned{tilted.x * std::cos(sweep) - tilted.z * std::sin(sweep), tilted.y, tilted.x * std::sin(sweep) + tilted.z * std::cos(sweep)};
    const QcVec3 right = rightOarlock + turned;
    return side < 0 ? QcVec3{-right.x, right.y, right.z} : right;
}

QcVec3 partPoint(int side, Paddle paddle, const QcVec3 &corner)
{
    // The left paddle's boxes are mirrored already: mirrored back, then placed as the mirror image,
    // so the whole stays a proper turn and its faces keep their winding.
    return paddlePoint(side, paddle, side < 0 ? QcVec3{-corner.x, corner.y, corner.z} : corner);
}

QcVec3 gripOf(int side, Paddle paddle)
{
    return paddlePoint(side, paddle, {gripAt, 0.0, 0.0});
}

double rowPhase(double seconds, bool rowing)
{
    if (!rowing) {
        return -1.0;
    }
    const double strokes = seconds / strokeSeconds;
    return strokes - std::floor(strokes);
}

QString planksOf(const QcContentRegistry &content, QcItemId item)
{
    if (!content.isValidItem(item)) {
        return {};
    }
    const std::optional<QcBlockId> block = content.blockId(content.item(item).boatBlock);
    return block ? content.block(*block).texture(QcBlockFace::North) : QString();
}

double bob(double seconds, quint32 id)
{
    const double phase = static_cast<double>(id % 97) * 0.61;
    return bobHeight * std::sin(seconds * 2.0 * std::numbers::pi / bobPeriod + phase);
}

} // namespace QcBoatModel
