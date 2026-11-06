// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/blending/QcBlendGrid.h"

#include "common/core/QcBlockPos.h"
#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

// Below and above these shares a column is copied whole from one version: the blend would differ
// from it by less than a block but for the greatest differences of height.
constexpr float wholeShare = 1.0F / 256.0F;
// The size of the patches of ground blocks.
constexpr double patchScale = 11.0;

// The four neighbours of a column.
constexpr std::array<std::pair<int, int>, 4> neighbours{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// A value from 0 to 1, in patches with frayed borders: below the share a column shows the new
// version's blocks.
class PatchChooser
{
public:
    explicit PatchChooser(quint64 seed)
        : m_seed(qcMixSeed(seed, 87))
        , m_noise(m_seed)
    {
    }

    bool showsNewer(int x, int z, float share) const
    {
        const double patch = 0.5 + 0.8 * m_noise.fractal2(x / patchScale, z / patchScale, 2);
        const double fray = 0.3 * (qcPositionRandom(m_seed, x, 0, z) - 0.5);
        return std::clamp(patch + fray, 0.0, 1.0) < share;
    }

private:
    quint64 m_seed;
    QcNoise m_noise;
};

int lerped(int from, int to, float share)
{
    return static_cast<int>(std::lround(from + (to - from) * static_cast<double>(share)));
}

// The column between `older` and `newer` at `share`.
QcBlendGrid::Cell blended(QcColumnShape older, QcColumnShape newer, float share)
{
    QcBlendGrid::Cell cell;
    cell.share = share;
    cell.wetInVersion = older.water >= 0 || newer.water >= 0;
    if (share < wholeShare || share > 1.0F - wholeShare) {
        const QcColumnShape &source = share < wholeShare ? older : newer;
        cell.whole = true;
        cell.newerSurface = share >= wholeShare;
        cell.height = source.ground;
        cell.water = source.water;
        return cell;
    }
    cell.height = lerped(older.ground, newer.ground, share);
    const int surface = lerped(older.surface(), newer.surface(), share);
    cell.water = surface > cell.height ? surface : -1;
    return cell;
}

} // namespace

QcBlendGrid::QcBlendGrid(const QcBlendField &field, const QcBlendSources &sources, int seaTop, quint64 seed)
    : m_seaTop(seaTop)
{
    const PatchChooser patches(seed);
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        const QcChunkPos pos = it.key();
        const QcBlendField::Shares shares = field.sharesIn(pos);
        ChunkCells cells;
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                const auto index = static_cast<std::size_t>(QcBlendField::columnIndex(x, z));
                Cell cell = blended(it->olderShape->column(x, z), it->newerShape->column(x, z), shares[index]);
                const bool newerPatch = patches.showsNewer(pos.x * QcWorld::chunkSize + x, pos.z * QcWorld::chunkSize + z, cell.share);
                cell.newerSurface = cell.whole ? cell.newerSurface : newerPatch;
                cells[index] = cell;
            }
        }
        m_cells.insert(pos, cells);
    }
    floodLowLand();
    // Banks only rise and water only falls, so this ends.
    do {
        settleWater();
    } while (raiseBanks());
    sealCaves();
}

const QcBlendGrid::Cell *QcBlendGrid::cell(int x, int z) const
{
    const QcBlockPos pos{x, 0, z};
    const auto found = m_cells.constFind(pos.chunk());
    return found == m_cells.cend() ? nullptr : &(*found)[static_cast<std::size_t>(QcBlendField::columnIndex(pos.localX(), pos.localZ()))];
}

QcBlendGrid::Cell *QcBlendGrid::cell(int x, int z)
{
    const QcBlockPos pos{x, 0, z};
    const auto found = m_cells.find(pos.chunk());
    return found == m_cells.end() ? nullptr : &(*found)[static_cast<std::size_t>(QcBlendField::columnIndex(pos.localX(), pos.localZ()))];
}

template<typename Visit>
void QcBlendGrid::eachCell(Visit visit)
{
    constexpr int columns = static_cast<int>(QcBlendField::columnCount);
    for (auto it = m_cells.begin(); it != m_cells.end(); ++it) {
        for (int i = 0; i < columns; ++i) {
            const int x = it.key().x * QcWorld::chunkSize + i % QcWorld::chunkSize;
            const int z = it.key().z * QcWorld::chunkSize + i / QcWorld::chunkSize;
            visit(x, z, (*it)[static_cast<std::size_t>(i)]);
        }
    }
}

void QcBlendGrid::floodLowLand()
{
    if (m_seaTop < 0) {
        return;
    }
    std::vector<std::pair<int, int>> open;
    eachCell([this, &open](int x, int z, Cell &cell) {
        if (cell.whole ? cell.water >= m_seaTop : cell.wetInVersion && cell.height < m_seaTop) {
            cell.water = cell.whole ? cell.water : std::max(cell.water, m_seaTop);
            open.emplace_back(x, z);
        }
    });
    while (!open.empty()) {
        const auto [x, z] = open.back();
        open.pop_back();
        for (const auto &[stepX, stepZ] : neighbours) {
            Cell *next = cell(x + stepX, z + stepZ);
            if (next != nullptr && !next->whole && next->height < m_seaTop && next->water < m_seaTop) {
                next->water = m_seaTop;
                open.emplace_back(x + stepX, z + stepZ);
            }
        }
    }
}

bool QcBlendGrid::raiseBanks()
{
    bool changed = false;
    eachCell([this, &changed](int x, int z, const Cell &whole) {
        if (whole.whole) {
            changed = (whole.water >= 0 ? raiseBanksBeside(x, z, whole.water) : banksAgainst(x, z, whole.height)) || changed;
        }
    });
    return changed;
}

bool QcBlendGrid::raiseBanksBeside(int x, int z, int water)
{
    bool changed = false;
    for (const auto &[stepX, stepZ] : neighbours) {
        Cell *next = cell(x + stepX, z + stepZ);
        if (next != nullptr && !next->whole && std::max(next->height, next->water) < water) {
            next->height = water;
            next->water = -1;
            changed = true;
        }
    }
    return changed;
}

bool QcBlendGrid::banksAgainst(int x, int z, int ground)
{
    bool changed = false;
    for (const auto &[stepX, stepZ] : neighbours) {
        Cell *next = cell(x + stepX, z + stepZ);
        if (next != nullptr && !next->whole && next->water > ground) {
            next->height = next->water;
            next->water = -1;
            changed = true;
        }
    }
    return changed;
}

void QcBlendGrid::sealCaves()
{
    eachCell([this](int x, int z, const Cell &wet) {
        if (wet.water >= 0) {
            sealBeside(x, z, wet);
        }
    });
}

void QcBlendGrid::sealBeside(int x, int z, const Cell &wet)
{
    for (const auto &[stepX, stepZ] : neighbours) {
        // Two columns copied whole stand as their version made them.
        if (Cell *next = cell(x + stepX, z + stepZ); next != nullptr && !(wet.whole && next->whole)) {
            next->sealedFrom = std::min(next->sealedFrom, wet.height + 1);
            next->sealedTo = std::max(next->sealedTo, wet.water);
        }
    }
}

void QcBlendGrid::settleWater()
{
    std::vector<std::pair<int, int>> open;
    eachCell([this, &open](int x, int z, const Cell &wet) {
        if (!wet.whole && wet.water > m_seaTop) {
            open.emplace_back(x, z);
        }
    });
    while (!open.empty()) {
        const auto [x, z] = open.back();
        open.pop_back();
        Cell &wet = *cell(x, z);
        const int before = wet.water;
        for (const auto &[stepX, stepZ] : neighbours) {
            const Cell *next = std::as_const(*this).cell(x + stepX, z + stepZ);
            if (next != nullptr && wet.water > m_seaTop) {
                wet.water = std::max({m_seaTop, std::min(wet.water, std::max(next->height, next->water))});
            }
        }
        if (wet.water <= wet.height) {
            wet.water = -1;
        }
        if (wet.water == before) {
            continue;
        }
        for (const auto &[stepX, stepZ] : neighbours) {
            const Cell *next = std::as_const(*this).cell(x + stepX, z + stepZ);
            if (next != nullptr && !next->whole && next->water > m_seaTop) {
                open.emplace_back(x + stepX, z + stepZ);
            }
        }
    }
}
