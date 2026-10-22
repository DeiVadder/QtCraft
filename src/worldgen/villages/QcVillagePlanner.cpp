#include "worldgen/villages/QcVillagePlanner.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"
#include "worldgen/villages/QcVillageRoads.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

// Pieces keep this many blocks to the region's edge besides the radius.
constexpr int regionMargin = 8;
// How far the bell in the meeting point may lie from the chosen centre.
constexpr int centreReach = 8;

} // namespace

QcVillagePlanner::QcVillagePlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyles &styles,
                                   quint64 seed)
    : m_terrain(terrain)
    , m_content(content)
    , m_styles(styles)
    , m_settings(content.villageCatalog().settings())
    , m_spacing(spacingOf(m_settings))
    , m_roads(terrain, content)
    , m_pieces(terrain, content, styles)
{
    m_seeds.layout = qcMixSeed(seed, qcNameSalt(u"qtcraft:villages"_s));
    m_seeds.variant = qcMixSeed(m_seeds.layout, qcNameSalt(u"qtcraft:village_variants"_s));
    m_seeds.crop = qcMixSeed(m_seeds.layout, qcNameSalt(u"qtcraft:village_crops"_s));
}

int QcVillagePlanner::spacingOf(const QcVillageSettings &settings)
{
    // Regions are whole chunks, so a chunk lies in exactly one region.
    return std::max(QcWorld::chunkSize * 8, settings.spacing / QcWorld::chunkSize * QcWorld::chunkSize);
}

int QcVillagePlanner::freeMargin(const QcVillageSettings &settings)
{
    // Centres keep this much to the region's edges (chooseCentre), the village the radius to its centre.
    return std::max(settings.radius + regionMargin, spacingOf(settings) / 4) - settings.radius;
}

const QcVillageStyle *QcVillagePlanner::styleAt(int x, int z) const
{
    const QcTerrainColumn column = m_terrain.column(x, z);
    if (column.landFactor < 0.99 || column.overhang >= 1.0) {
        return nullptr;
    }
    const QcBiomeDefinition *biome = m_content.worldgen().biome(m_terrain.biomeName(column.biome));
    const QcVillageStyle *style = biome ? m_styles.forBiome(*biome) : nullptr;
    // A style over water stands on wet ground too, one on land only above the sea level.
    if (!style || (!style->overWater && column.height - 1 <= m_terrain.seaLevel())) {
        return nullptr;
    }
    return style;
}

bool QcVillagePlanner::villageBiomeAround(int x, int z) const
{
    static constexpr std::array<std::array<int, 2>, 4> corners{{{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}};
    for (const auto &[sx, sz] : corners) {
        const QcTerrainColumn column = m_terrain.column(x + sx * centreReach, z + sz * centreReach);
        const QcBiomeDefinition *biome = m_content.worldgen().biome(m_terrain.biomeName(column.biome));
        // A river through the village is crossed by bridges (decision E-841).
        if (m_terrain.isRiverBiome(column.biome)) {
            continue;
        }
        if (!biome || !biome->villages) {
            return false;
        }
    }
    return true;
}

bool QcVillagePlanner::isFlatAround(const QcVillageStyle &style, int x, int z) const
{
    const int reach = m_settings.radius / 2 / flatnessStep * flatnessStep;
    if (style.overWater) {
        return wetAround(style, x, z).has_value();
    }
    int low = QcWorld::worldHeight;
    int high = 0;
    for (int dz = -reach; dz <= reach; dz += flatnessStep) {
        for (int dx = -reach; dx <= reach; dx += flatnessStep) {
            // A river and the low ground of its valley are crossed by bridges (decision E-841): the
            // land around them must be flat.
            const QcTerrainColumn column = m_terrain.column(x + dx, z + dz);
            if (column.riverDistance < column.riverHalfWidth + riverValley) {
                continue;
            }
            low = std::min(low, column.height);
            high = std::max(high, column.height);
        }
    }
    return high - low <= m_settings.maxHeightSpread;
}

std::optional<int> QcVillagePlanner::wetAround(const QcVillageStyle &style, int x, int z) const
{
    const int reach = m_settings.radius / 2 / flatnessStep * flatnessStep;
    return QcBoardwalkPlanner::wetAround(m_terrain, style, {x, z}, reach, flatnessStep);
}

std::optional<QcVillageDraft::Centre> QcVillagePlanner::chooseCentre(int regionX, int regionZ) const
{
    // The first of a few places in the region that is flat land of a biome with villages. Centres
    // keep a quarter of the region to its edges, so neighbouring villages lie half a region apart.
    // Once a place over water fits, the later places are tried too and the wettest one wins: a
    // village over water stands where its biome has the most water.
    const int margin = m_settings.radius + freeMargin(m_settings);
    const int room = std::max(1, m_spacing - 2 * margin);
    std::optional<QcVillageDraft::Centre> chosen;
    int wettest = -1;
    for (int attempt = 0; attempt < m_settings.attempts && (!chosen || chosen->style->overWater); ++attempt) {
        const int x = regionX * m_spacing + margin + int(qcPositionRandom(m_seeds.layout, regionX, -1 - 2 * attempt, regionZ) * room);
        const int z = regionZ * m_spacing + margin + int(qcPositionRandom(m_seeds.layout, regionX, -2 - 2 * attempt, regionZ) * room);
        const QcVillageStyle *style = styleAt(x, z);
        if (!style || (chosen && !style->overWater) || !villageBiomeAround(x, z)) {
            continue;
        }
        const std::optional<int> wet = style->overWater ? wetAround(*style, x, z) : std::nullopt;
        const bool fits = style->overWater ? wet && *wet > wettest : isFlatAround(*style, x, z);
        if (fits) {
            chosen = QcVillageDraft::Centre{style, x, z};
            wettest = wet.value_or(-1);
        }
    }
    return chosen;
}

std::shared_ptr<const QcVillagePlan> QcVillagePlanner::plan(int regionX, int regionZ) const
{
    if (qcPositionRandom(m_seeds.layout, regionX, 0, regionZ) >= m_settings.chance || m_styles.isEmpty()) {
        return nullptr;
    }
    const std::optional<QcVillageDraft::Centre> centre = chooseCentre(regionX, regionZ);
    if (!centre) {
        return nullptr;
    }

    QcVillageDraft draft(m_seeds, {regionX, regionZ, m_spacing, m_settings.radius}, *centre);
    draft.start();
    draft.setLayout(QcRoadNetworkPlanner::chooseLayout(draft));
    m_pieces.placeCentre(draft);
    if (draft.plan.pieces.isEmpty()) {
        return nullptr;
    }

    const QList<QcVillageDraft::Road> roads = m_roads.layRoads(draft);
    m_pieces.placeRulePieces(draft, roads);
    for (const QcVillageDraft::Road &road : roads) {
        m_pieces.placeAlong(draft, road);
    }
    for (const QcVillageDraft::Road &road : roads) {
        m_pieces.placeLamps(draft, road);
    }
    // After the pieces: the forecourts of pieces at diagonal roads are side road too.
    draft.plan.steps = QcVillageRoads::steps(draft.plan, draft.pathStep, draft.branchStep);

    draft.coverArea();
    draft.plan.squeeze();
    return std::make_shared<const QcVillagePlan>(std::move(draft.plan));
}
