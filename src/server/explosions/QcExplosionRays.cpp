#include "server/explosions/QcExplosionRays.h"

#include "common/content/definitions/QcBlastDefinition.h"
#include "common/physics/QcPhysics.h"
#include "server/explosions/QcBlastField.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

// The force of a ray between 0.7 and 1.3 of the power, by SplitMix64 over the seed, the centre's
// cell and the ray.
double forceOf(const QcExplosionRays::Blast &blast, QcBlockPos centre, int ray)
{
    quint64 h = blast.seed ^ 0x9E3779B97F4A7C15ull;
    const auto word = [](int value) { return quint64(quint32(value)); };
    for (const quint64 value : {word(centre.x), word(centre.y), word(centre.z), word(ray)}) {
        h += value + 0x9E3779B97F4A7C15ull;
        h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
        h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
        h ^= h >> 31;
    }
    const double unit = double(h >> 11) / double(1ull << 53);
    return blast.power * (0.7 + 0.6 * unit);
}

// Whether (i, j, k) of the grid lies on the surface of its cube.
bool onSurface(int i, int j, int k)
{
    constexpr int last = QcExplosionRays::grid - 1;
    return i == 0 || i == last || j == 0 || j == last || k == 0 || k == last;
}

// The cells the rays of a blast break, collected ray by ray.
struct BrokenCells
{
    QSet<QcBlockPos> cells;

    // Walks one ray and adds the cells it breaks.
    void addRay(QcBlastField &field, const QcVec3 &centre, QcVec3 direction, double force)
    {
        direction = direction * (QcExplosionRays::stepLength / direction.length());
        QcVec3 point = centre;
        while (force > 0.0) {
            const QcBlockPos cell = QcPhysics::blockContaining(point);
            const QcBlastField::Cell &at = field.at(cell);
            if (!at.loaded) {
                return;
            }
            if (at.block != qcAirBlockId) {
                force -= (double(at.resistance) + 0.3) * QcExplosionRays::stepLength;
                if (force > 0.0 && at.resistance < float(QcBlastDefinition::unbreakable)) {
                    cells.insert(cell);
                }
            }
            point += direction;
            force -= QcExplosionRays::stepCost;
        }
    }
};

double distanceSquared(QcBlockPos cell, const QcVec3 &centre)
{
    const QcVec3 middle{cell.x + 0.5, cell.y + 0.5, cell.z + 0.5};
    const QcVec3 delta = middle - centre;
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}

// Whether the segment from `from` to `to` passes no solid block (steps of a quarter block).
bool sees(QcBlastField &field, const QcVec3 &from, const QcVec3 &to)
{
    const QcVec3 delta = to - from;
    const int steps = std::max(1, int(std::ceil(delta.length() * 4.0)));
    const QcBlockPos last = QcPhysics::blockContaining(to);
    for (int step = 1; step < steps; ++step) {
        const QcBlockPos cell = QcPhysics::blockContaining(from + delta * (double(step) / steps));
        if (cell == last) {
            break;
        }
        const QcBlastField::Cell &at = field.at(cell);
        if (!at.loaded || at.solid) {
            return false;
        }
    }
    return true;
}

// Every ray of the blast; the cells they break.
QSet<QcBlockPos> castRays(QcBlastField &field, const QcExplosionRays::Blast &blast)
{
    constexpr int grid = QcExplosionRays::grid;
    constexpr double half = (grid - 1) / 2.0;
    BrokenCells broken;
    const QcBlockPos centre = QcPhysics::blockContaining(blast.centre);
    int ray = 0;
    // The cells of the grid in the order i, j, k; only those on its surface send a ray.
    for (int cell = 0; cell < grid * grid * grid; ++cell) {
        const int i = cell / (grid * grid);
        const int j = cell / grid % grid;
        const int k = cell % grid;
        if (onSurface(i, j, k)) {
            const QcVec3 direction{i / half - 1.0, j / half - 1.0, k / half - 1.0};
            broken.addRay(field, blast.centre, direction, forceOf(blast, centre, ray++));
        }
    }
    return std::move(broken.cells);
}

} // namespace

namespace QcExplosionRays {

int reach(double power)
{
    // The longest ray: 1.3 × power of force at stepCost per step of stepLength blocks.
    return int(std::ceil(1.3 * power / stepCost * stepLength)) + 1;
}

QList<QcBlockPos> broken(QcBlastField &field, const Blast &blast, qsizetype limit)
{
    if (limit <= 0) {
        return {};
    }

    const QSet<QcBlockPos> cells = castRays(field, blast);
    QList<QcBlockPos> ordered(cells.begin(), cells.end());
    std::ranges::sort(ordered, [&blast](QcBlockPos a, QcBlockPos b) {
        const double da = distanceSquared(a, blast.centre);
        const double db = distanceSquared(b, blast.centre);
        if (da != db) {
            return da < db;
        }
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    });
    if (ordered.size() > limit) {
        ordered.resize(limit);
    }
    return ordered;
}

double exposure(QcBlastField &field, const QcVec3 &centre, const QcAabb &box)
{
    // Like the reference game: a grid over the box with about two points per block of its size.
    const QcVec3 size = box.max - box.min;
    const auto points = [](double extent) { return std::max(2, int(std::floor(extent * 2.0)) + 2); };
    const int nx = points(size.x);
    const int ny = points(size.y);
    const int nz = points(size.z);
    int seen = 0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                const QcVec3 point{box.min.x + size.x * i / (nx - 1), box.min.y + size.y * j / (ny - 1), box.min.z + size.z * k / (nz - 1)};
                seen += sees(field, point, centre) ? 1 : 0;
            }
        }
    }
    return double(seen) / double(nx * ny * nz);
}

} // namespace QcExplosionRays
