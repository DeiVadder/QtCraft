#include "worldgen/structures/QcStructurePlacement.h"

#include "worldgen/structures/QcStateRotation.h"

#include <algorithm>

QcStructurePlacement::QcStructurePlacement(std::shared_ptr<const QcStructure> structure, QcBlockPos origin, int turns,
                                           const QcContentRegistry &content)
    : m_structure(std::move(structure))
    , m_origin(origin)
    , m_turns(((turns % 4) + 4) % 4)
{
    Q_ASSERT(m_structure);
    m_palette.reserve(m_structure->palette().size());
    for (const QcBlockId state : m_structure->palette()) {
        m_palette.append(QcStateRotation::turned(content, state, m_turns));
    }
}

bool QcStructurePlacement::overlaps(QcChunkPos chunk) const
{
    const int chunkX = chunk.x * QcWorld::chunkSize;
    const int chunkZ = chunk.z * QcWorld::chunkSize;
    return m_origin.x < chunkX + QcWorld::chunkSize && m_origin.x + sizeX() > chunkX && m_origin.z < chunkZ + QcWorld::chunkSize
           && m_origin.z + sizeZ() > chunkZ;
}

QcBlockPos QcStructurePlacement::toWorld(QcBlockPos local) const
{
    const int sx = m_structure->sizeX();
    const int sz = m_structure->sizeZ();
    int x = local.x;
    int z = local.z;
    switch (m_turns) {
    case 1:
        x = sz - 1 - local.z;
        z = local.x;
        break;
    case 2:
        x = sx - 1 - local.x;
        z = sz - 1 - local.z;
        break;
    case 3:
        x = local.z;
        z = sx - 1 - local.x;
        break;
    default:
        break;
    }
    return {m_origin.x + x, m_origin.y + local.y, m_origin.z + z};
}

QcVec3 QcStructurePlacement::toWorld(const QcVec3 &local) const
{
    const double sx = m_structure->sizeX();
    const double sz = m_structure->sizeZ();
    double x = local.x;
    double z = local.z;
    switch (m_turns) {
    case 1:
        x = sz - local.z;
        z = local.x;
        break;
    case 2:
        x = sx - local.x;
        z = sz - local.z;
        break;
    case 3:
        x = local.z;
        z = sx - local.x;
        break;
    default:
        break;
    }
    return {m_origin.x + x, m_origin.y + local.y, m_origin.z + z};
}

std::optional<QcBlockId> QcStructurePlacement::worldBlock(QcBlockPos local) const
{
    if (!m_structure->contains(local.x, local.y, local.z)) {
        return std::nullopt;
    }

    const qint16 index = m_structure->paletteIndex(local.x, local.y, local.z);
    if (index < 0) {
        return std::nullopt;
    }
    return m_palette[index];
}

std::pair<int, int> QcStructurePlacement::localColumn(int offsetX, int offsetZ) const
{
    const int sx = m_structure->sizeX();
    const int sz = m_structure->sizeZ();
    switch (m_turns) {
    case 1:
        return {offsetZ, sz - 1 - offsetX};
    case 2:
        return {sx - 1 - offsetX, sz - 1 - offsetZ};
    case 3:
        return {sx - 1 - offsetZ, offsetX};
    default:
        return {offsetX, offsetZ};
    }
}

void QcStructurePlacement::writeColumn(QcChunk &chunk, int worldX, int worldZ) const
{
    const auto [lx, lz] = localColumn(worldX - m_origin.x, worldZ - m_origin.z);
    const int lowY = std::max(0, -m_origin.y);
    const int highY = std::min(m_structure->sizeY(), QcWorld::worldHeight - m_origin.y);
    for (int y = lowY; y < highY; ++y) {
        const qint16 index = m_structure->paletteIndex(lx, y, lz);
        if (index >= 0) {
            chunk.setBlock(worldX - chunk.pos().x * QcWorld::chunkSize, m_origin.y + y, worldZ - chunk.pos().z * QcWorld::chunkSize,
                           m_palette[index]);
        }
    }
}

void QcStructurePlacement::writeInto(QcChunk &chunk) const
{
    const int chunkX = chunk.pos().x * QcWorld::chunkSize;
    const int chunkZ = chunk.pos().z * QcWorld::chunkSize;
    for (int wz = std::max(chunkZ, m_origin.z); wz < std::min(chunkZ + QcWorld::chunkSize, m_origin.z + sizeZ()); ++wz) {
        for (int wx = std::max(chunkX, m_origin.x); wx < std::min(chunkX + QcWorld::chunkSize, m_origin.x + sizeX()); ++wx) {
            writeColumn(chunk, wx, wz);
        }
    }
}
