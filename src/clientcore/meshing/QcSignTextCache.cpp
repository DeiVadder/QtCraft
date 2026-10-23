#include "clientcore/meshing/QcSignTextCache.h"

#include "common/core/QcWorldConstants.h"

#include <algorithm>
#include <cmath>

void QcSignTextCache::setSigns(const QHash<QcBlockPos, QcSignText::Text> &signs, quint64 generation)
{
    if (m_generation == generation) {
        return;
    }
    m_generation = generation;

    QHash<QcBlockPos, Sign> kept;
    m_chunks.clear();
    for (auto it = signs.cbegin(); it != signs.cend(); ++it) {
        Sign sign = m_signs.take(it.key());
        if (sign.text != it.value()) {
            sign = Sign{it.value(), std::nullopt, {-1, -1}, {}};
        }
        kept.emplace(it.key(), std::move(sign));
        m_chunks[it.key().chunk()].append(it.key());
    }
    m_signs = std::move(kept);
}

void QcSignTextCache::clear()
{
    m_signs.clear();
    m_chunks.clear();
    m_generation.reset();
}

QList<QcBlockPos> QcSignTextCache::signsNear(const QcVec3 &viewer, double distance) const
{
    QList<QcBlockPos> near;
    for (auto chunk = m_chunks.cbegin(); chunk != m_chunks.cend(); ++chunk) {
        // The horizontal distance from the viewer to the nearest point of the chunk.
        const double west = static_cast<double>(chunk.key().x * QcWorld::chunkSize);
        const double north = static_cast<double>(chunk.key().z * QcWorld::chunkSize);
        const double dx = std::max({west - viewer.x, 0.0, viewer.x - west - QcWorld::chunkSize});
        const double dz = std::max({north - viewer.z, 0.0, viewer.z - north - QcWorld::chunkSize});
        if (dx * dx + dz * dz > distance * distance) {
            continue;
        }

        for (const QcBlockPos pos : chunk.value()) {
            const QcVec3 middle{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
            if ((middle - viewer).length() <= distance) {
                near.append(pos);
            }
        }
    }
    return near;
}

const std::vector<QcSignTextMesher::Quad> &QcSignTextCache::quads(QcBlockPos pos, QcBlockId block, QcEntityLight light,
                                                                  const QcContentRegistry &content, const QcEntityMesher::UvLookup &uv)
{
    static const std::vector<QcSignTextMesher::Quad> none;
    const auto found = m_signs.find(pos);
    if (found == m_signs.end()) {
        return none;
    }

    Sign &sign = *found;
    const std::pair<int, int> key = lightKey(light);
    if (sign.block == block && sign.light == key) {
        return sign.quads;
    }

    sign.block = block;
    sign.light = key;
    const std::optional<QcSignTextMesher::Face> face = QcSignTextMesher::faceOf(content, block, pos);
    sign.quads = face ? QcSignTextMesher::textQuads(*face, sign.text, light, uv) : std::vector<QcSignTextMesher::Quad>{};
    ++m_builds;
    return sign.quads;
}

std::pair<int, int> QcSignTextCache::lightKey(QcEntityLight light)
{
    return {static_cast<int>(std::lround(light.sky * lightSteps)), static_cast<int>(std::lround(light.block * lightSteps))};
}
