#include "worldgen/surface/QcSurfacePlanner.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QcSurfacePlanner::QcSurfacePlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_terrain(terrain)
    , m_content(content)
{
    for (const QcStructureDefinition &definition : content.worldgen().structures()) {
        if (!definition.surface) {
            continue;
        }
        const QcPieceVariation::Resolved structure = QcPieceVariation::resolve(definition, content);
        m_warnings.append(QcPieceVariation::warningsOf(structure));
        const std::optional<QcBlockId> foundation =
            definition.surface->foundation.isEmpty() ? std::optional<QcBlockId>(qcAirBlockId) : content.blockId(definition.surface->foundation);
        if (!structure || !foundation) {
            m_warnings.append(u"structure '%1': not placed on the surface (%2)"_s.arg(
                definition.name, structure ? u"unknown foundation '%1'"_s.arg(definition.surface->foundation) : structure.error().error));
            continue;
        }
        m_kinds.append({definition.name, *structure, *definition.surface, *foundation, qcMixSeed(seed, qcNameSalt(definition.name))});
    }
    std::ranges::sort(m_kinds, {}, &Kind::name);
}

std::optional<int> QcSurfacePlanner::kindIndex(const QString &name) const
{
    for (qsizetype i = 0; i < m_kinds.size(); ++i) {
        if (m_kinds[i].name == name) {
            return int(i);
        }
    }
    return std::nullopt;
}

QcSurfacePlanner::Rect QcSurfacePlanner::footprintOf(const QcStructurePlacement &placement)
{
    const QcBlockPos origin = placement.origin();
    return {origin.x, origin.z, origin.x + placement.sizeX() - 1, origin.z + placement.sizeZ() - 1};
}

std::optional<QcStructurePlacement> QcSurfacePlanner::plan(int kind, int regionX, int regionZ, const Occupied &occupied) const
{
    const Kind &type = m_kinds[kind];
    const QcStructureDefinition::Surface &rule = type.rule;
    if (qcPositionRandom(type.seed, regionX, 0, regionZ) >= rule.chance) {
        return std::nullopt;
    }

    // Every attempt draws its centre and turn from its own positions, so one attempt never shifts another.
    const int width = std::max(type.structure()->sizeX(), type.structure()->sizeZ());
    const int room = rule.spacing - width;
    std::optional<QcStructurePlacement> first;
    for (int attempt = 0; attempt < rule.attempts; ++attempt) {
        const int turns = int(qcPositionRandom(type.seed, regionX, 3 * attempt + 1, regionZ) * 4.0) % 4;
        const int sizeX = turns % 2 == 0 ? type.structure()->sizeX() : type.structure()->sizeZ();
        const int sizeZ = turns % 2 == 0 ? type.structure()->sizeZ() : type.structure()->sizeX();
        const int x0 = regionX * rule.spacing + int(qcPositionRandom(type.seed, regionX, 3 * attempt + 2, regionZ) * room);
        const int z0 = regionZ * rule.spacing + int(qcPositionRandom(type.seed, regionX, 3 * attempt + 3, regionZ) * room);
        const Rect rect{x0, z0, x0 + sizeX - 1, z0 + sizeZ - 1};
        const std::optional<int> floor = floorFor(type, rect);
        if (!floor || occupied(kind, rect, rule.villageMargin)) {
            continue;
        }
        QcStructurePlacement placement(structureOf(type, regionX, regionZ), {x0, *floor - type.structure()->groundLayer, z0}, turns, m_content);
        const QcTerrainColumn middle = m_terrain.column((rect.x0 + rect.x1) / 2, (rect.z0 + rect.z1) / 2);
        if (!rule.preferWater || middle.height < m_terrain.seaLevel()) {
            return placement;
        }
        if (!first) {
            first = std::move(placement);
        }
    }
    return first;
}

std::shared_ptr<const QcStructure> QcSurfacePlanner::structureOf(const Kind &kind, int regionX, int regionZ)
{
    if (!kind.variation.varies()) {
        return kind.structure();
    }
    // Drawn below the positions of the attempts (0 and up), so the place stays where it was.
    int draw = 0;
    return kind.variation.build(kind.variation.choose([&] { return qcPositionRandom(kind.seed, regionX, -1 - draw++, regionZ); }));
}

std::optional<int> QcSurfacePlanner::floorFor(const Kind &kind, Rect rect) const
{
    // The middle and the edges every few blocks, corners included: no hill or hollow along an edge
    // goes unseen.
    QList<std::pair<int, int>> samples{{(rect.x0 + rect.x1) / 2, (rect.z0 + rect.z1) / 2}, {rect.x1, rect.z0}, {rect.x1, rect.z1}};
    for (int x = rect.x0; x < rect.x1; x += edgeStep) {
        samples.append({{x, rect.z0}, {x, rect.z1}});
    }
    for (int z = rect.z0 + edgeStep; z < rect.z1; z += edgeStep) {
        samples.append({{rect.x0, z}, {rect.x1, z}});
    }

    int low = QcWorld::worldHeight;
    int high = 0;
    for (const auto &[x, z] : std::as_const(samples)) {
        const QcTerrainColumn column = m_terrain.column(x, z);
        // Land above the sea, or with `waterDepth` ground under at most that much water (the
        // terrain fills water from a column's height up to the sea level, exclusive).
        const bool ground = kind.rule.waterDepth > 0 ? m_terrain.seaLevel() - column.height <= kind.rule.waterDepth
                                                     : column.height - 1 >= m_terrain.seaLevel();
        if (!ground || column.overhang >= 1.0 || !kind.rule.biomes.contains(m_terrain.biomeName(column.biome))) {
            return std::nullopt;
        }
        // A river narrower than the samples' spacing may run between them (decision E-840): on land
        // no sample lies within a block of a river's water.
        if (kind.rule.waterDepth == 0 && column.riverDistance < column.riverHalfWidth + 1.0) {
            return std::nullopt;
        }
        low = std::min(low, column.height);
        high = std::max(high, column.height);
    }
    if (high - low > kind.rule.maxSlope) {
        return std::nullopt;
    }

    // On the lowest ground: no edge, the entrance included, hangs above the ground; higher ground
    // over the footprint is cleared.
    const int floor = low - 1;
    const int top = floor - kind.structure()->groundLayer + kind.structure()->sizeY();
    if (floor - kind.structure()->groundLayer < 1 || top >= QcWorld::worldHeight) {
        return std::nullopt;
    }
    // Over water, nothing at or below the sea is cleared: the water around the stilts stays whole.
    if (kind.rule.clearTerrain && kind.rule.waterDepth > 0 && floor - kind.structure()->groundLayer + kind.rule.clearFrom < m_terrain.seaLevel()) {
        return std::nullopt;
    }
    return floor;
}
