#include "server/core/QcContactDamage.h"

#include "common/core/QcIntMath.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerWorld.h"

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

using Touch = QcContactDamage::Touch;

Touch touchOf(const std::vector<Touch> &table, QcBlockId id)
{
    return static_cast<std::size_t>(id) < table.size() ? table[static_cast<std::size_t>(id)] : Touch{};
}

// The stronger of both values of two touches.
Touch strongest(Touch a, Touch b)
{
    return {std::max(a.damage, b.damage), std::max(a.alightTicks, b.alightTicks), a.burnsItems || b.burnsItems};
}

// Highest contact damage and burning in `section` within `local` (coordinates local to the section).
Touch touchInSection(const QcChunkSection &section, const BlockBounds &local, const std::vector<Touch> &table)
{
    Touch touch;
    for (int x = local.minX; x <= local.maxX; ++x) {
        for (int y = local.minY; y <= local.maxY; ++y) {
            for (int z = local.minZ; z <= local.maxZ; ++z) {
                touch = strongest(touch, touchOf(table, section.block(x, y, z)));
            }
        }
    }
    return touch;
}

// Highest contact damage and burning in the sections of `chunk` marked in `mask`, within `local`
// (x and z local to the chunk, y in the world).
Touch touchInChunk(const QcChunk &chunk, quint32 mask, const BlockBounds &local, const std::vector<Touch> &table)
{
    constexpr int height = QcWorld::sectionHeight;
    Touch touch;
    for (int index = local.minY / height; index <= local.maxY / height; ++index) {
        if ((mask & (1u << index)) == 0) {
            continue;
        }

        BlockBounds inSection = local;
        inSection.minY = std::max(local.minY, index * height) - index * height;
        inSection.maxY = std::min(local.maxY, index * height + height - 1) - index * height;
        touch = strongest(touch, touchInSection(chunk.section(index), inSection, table));
    }
    return touch;
}

} // namespace

QcContactDamage::QcContactDamage(const QcContentRegistry &content)
    : m_touch(static_cast<std::size_t>(content.stateCount()))
{
    for (std::size_t id = 0; id < m_touch.size(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        m_touch[id] = {block.contactDamage, static_cast<int>(std::lround(block.fire.setsAlightSeconds * QcPhysics::tickRate)),
                       block.contactDamage > 0.0 && block.burnsItems};
    }
}

bool QcContactDamage::isPossibleIn(const QcServerWorld &world) const
{
    return !world.marks().isEmpty(QcBlockTrait::ContactDamage);
}

double QcContactDamage::in(const QcServerWorld &world, const QcAabb &box) const
{
    return touching(world, box).damage;
}

QcContactDamage::Touch QcContactDamage::touching(const QcServerWorld &world, const QcAabb &box) const
{
    constexpr double inset = 1e-3;
    constexpr int size = QcWorld::chunkSize;

    BlockBounds bounds;
    bounds.minX = static_cast<int>(std::floor(box.min.x + inset));
    bounds.maxX = static_cast<int>(std::floor(box.max.x - inset));
    bounds.minZ = static_cast<int>(std::floor(box.min.z + inset));
    bounds.maxZ = static_cast<int>(std::floor(box.max.z - inset));

    // Outside the world height there is only air, which does no damage.
    bounds.minY = std::max(static_cast<int>(std::floor(box.min.y + inset)), 0);
    bounds.maxY = std::min(static_cast<int>(std::floor(box.max.y - inset)), QcWorld::worldHeight - 1);
    if (!isPossibleIn(world) || bounds.minX > bounds.maxX || bounds.minY > bounds.maxY || bounds.minZ > bounds.maxZ) {
        return {};
    }

    Touch touch;
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
            touch = strongest(touch, touchInChunk(*world.chunk(pos), sections, local, m_touch));
        }
    }
    return touch;
}
