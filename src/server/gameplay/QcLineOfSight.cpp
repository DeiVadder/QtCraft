#include "QcLineOfSight.h"

#include "QcRaycast.h"

#include <array>
#include <cmath>

namespace {

// The world with the cell of the eye read as air: a player whose eye is inside a visible box (sugar
// cane, a torch) still sees out of it, the ray would otherwise stop at distance 0.
class WithoutEyeCell : public QcBlockAccess
{
public:
    WithoutEyeCell(const QcBlockAccess &world, QcBlockPos eyeCell)
        : m_world(world)
        , m_eyeCell(eyeCell)
    {
    }

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        return pos == m_eyeCell ? std::optional<QcBlockId>(qcAirBlockId) : m_world.blockAt(pos);
    }
    const QcContentRegistry &content() const override { return m_world.content(); }

private:
    const QcBlockAccess &m_world;
    QcBlockPos m_eyeCell;
};

QcBlockPos cellOf(const QcVec3 &point)
{
    return {int(std::floor(point.x)), int(std::floor(point.y)), int(std::floor(point.z))};
}

bool reachesPoint(const QcBlockAccess &world, const QcVec3 &eye, const QcVec3 &point, std::optional<QcBlockPos> target)
{
    const QcVec3 delta = point - eye;
    const double distance = delta.length();
    if (distance < 1e-6) {
        return true;
    }

    const std::optional<QcRaycastHit> hit = QcRaycast::cast(world, eye, delta * (1.0 / distance), distance);
    return !hit || (target && hit->block == *target);
}

} // namespace

namespace QcLineOfSight {

bool reaches(const QcBlockAccess &fullWorld, const QcVec3 &eye, const QcAabb &box, std::optional<QcBlockPos> target)
{
    // The eye's own cell does not hide anything, unless it is the target itself.
    const QcBlockPos eyeCell = cellOf(eye);
    const WithoutEyeCell withoutEyeCell(fullWorld, eyeCell);
    const QcBlockAccess &world = target && *target == eyeCell ? fullWorld : withoutEyeCell;

    const QcVec3 centre = (box.min + box.max) * 0.5;
    if (reachesPoint(world, eye, centre, target)) {
        return true;
    }

    // Corners and face centres as fractions of the box.
    constexpr double low = 0.1;
    constexpr double high = 0.9;
    constexpr std::array<QcVec3, 14> samples{{
        {low, low, low}, {high, low, low}, {low, high, low}, {high, high, low},
        {low, low, high}, {high, low, high}, {low, high, high}, {high, high, high},
        {0.5, 0.5, low}, {0.5, 0.5, high}, {0.5, low, 0.5}, {0.5, high, 0.5}, {low, 0.5, 0.5}, {high, 0.5, 0.5},
    }};

    const QcVec3 size = box.max - box.min;
    for (const QcVec3 &fraction : samples) {
        const QcVec3 point = box.min + QcVec3{size.x * fraction.x, size.y * fraction.y, size.z * fraction.z};
        if (reachesPoint(world, eye, point, target)) {
            return true;
        }
    }
    return false;
}

QcAabb blockBox(QcBlockPos pos)
{
    const QcVec3 corner{double(pos.x), double(pos.y), double(pos.z)};
    return {corner, corner + QcVec3{1.0, 1.0, 1.0}};
}

} // namespace QcLineOfSight
