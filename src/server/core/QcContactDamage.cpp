#include "QcContactDamage.h"

#include "QcIntMath.h"
#include "QcServerWorld.h"

#include <algorithm>
#include <cmath>

namespace {

// Inclusive block bounds.
struct BlockBounds
{
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int minZ = 0;
    int maxZ = 0;
};

double contactDamageOf(const std::vector<double> &table, QcBlockId id)
{
    return std::size_t(id) < table.size() ? table[std::size_t(id)] : 0.0;
}

// Highest contact damage in `section` within `local` (coordinates local to the section).
double contactDamageInSection(const QcChunkSection &section, const BlockBounds &local, const std::vector<double> &table)
{
    double damage = 0.0;
    for (int x = local.minX; x <= local.maxX; ++x) {
        for (int y = local.minY; y <= local.maxY; ++y) {
            for (int z = local.minZ; z <= local.maxZ; ++z) {
                damage = std::max(damage, contactDamageOf(table, section.block(x, y, z)));
            }
        }
    }
    return damage;
}

// Highest contact damage in the sections of `chunk` marked in `mask`, within `local` (x and z
// local to the chunk, y in the world).
double contactDamageInChunk(const QcChunk &chunk, quint32 mask, const BlockBounds &local, const std::vector<double> &table)
{
    constexpr int height = QcWorld::sectionHeight;
    double damage = 0.0;
    for (int index = local.minY / height; index <= local.maxY / height; ++index) {
        if ((mask & (1u << index)) == 0) {
            continue;
        }

        BlockBounds inSection = local;
        inSection.minY = std::max(local.minY, index * height) - index * height;
        inSection.maxY = std::min(local.maxY, index * height + height - 1) - index * height;
        damage = std::max(damage, contactDamageInSection(chunk.section(index), inSection, table));
    }
    return damage;
}

} // namespace

QcContactDamage::QcContactDamage(const QcContentRegistry &content)
    : m_damage(std::size_t(content.stateCount()))
{
    for (std::size_t id = 0; id < m_damage.size(); ++id) {
        m_damage[id] = content.block(QcBlockId(id)).contactDamage;
    }
}

bool QcContactDamage::isPossibleIn(const QcServerWorld &world) const
{
    return !world.marks().isEmpty(QcBlockTrait::ContactDamage);
}

double QcContactDamage::in(const QcServerWorld &world, const QcAabb &box) const
{
    constexpr double inset = 1e-3;
    constexpr int size = QcWorld::chunkSize;

    BlockBounds bounds;
    bounds.minX = int(std::floor(box.min.x + inset));
    bounds.maxX = int(std::floor(box.max.x - inset));
    bounds.minZ = int(std::floor(box.min.z + inset));
    bounds.maxZ = int(std::floor(box.max.z - inset));

    // Outside the world height there is only air, which does no damage.
    bounds.minY = std::max(int(std::floor(box.min.y + inset)), 0);
    bounds.maxY = std::min(int(std::floor(box.max.y - inset)), QcWorld::worldHeight - 1);
    if (!isPossibleIn(world) || bounds.minX > bounds.maxX || bounds.minY > bounds.maxY || bounds.minZ > bounds.maxZ) {
        return 0.0;
    }

    double damage = 0.0;
    for (int chunkX = qcFloorDiv(bounds.minX, size); chunkX <= qcFloorDiv(bounds.maxX, size); ++chunkX) {
        for (int chunkZ = qcFloorDiv(bounds.minZ, size); chunkZ <= qcFloorDiv(bounds.maxZ, size); ++chunkZ) {
            const QcChunkPos pos{chunkX, chunkZ};
            const quint32 sections = world.marks().sectionsOf(pos, QcBlockTrait::ContactDamage);
            if (sections == 0) {
                continue;
            }

            BlockBounds local = bounds;
            local.minX = std::max(bounds.minX, chunkX * size) - chunkX * size;
            local.maxX = std::min(bounds.maxX, chunkX * size + size - 1) - chunkX * size;
            local.minZ = std::max(bounds.minZ, chunkZ * size) - chunkZ * size;
            local.maxZ = std::min(bounds.maxZ, chunkZ * size + size - 1) - chunkZ * size;
            damage = std::max(damage, contactDamageInChunk(*world.chunk(pos), sections, local, m_damage));
        }
    }
    return damage;
}
