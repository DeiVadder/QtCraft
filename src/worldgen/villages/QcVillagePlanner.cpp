#include "worldgen/villages/QcVillagePlanner.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"
#include "worldgen/villages/QcVillageRoads.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Pieces keep this many blocks to the region's edge besides the radius.
constexpr int regionMargin = 8;

// The lists of a finished plan without the room they grew into: the generator caches hundreds of
// plans (QcVillageGenerator::cachedPlans).
void squeeze(QcVillagePlan &plan)
{
    plan.pieces.squeeze();
    plan.roads.squeeze();
    plan.branchRoads.squeeze();
    plan.thresholds.squeeze();
    plan.runs.squeeze();
    plan.steps.squeeze();
    plan.decks.squeeze();
    plan.wetland.wet.squeeze();
    plan.wetland.dry.squeeze();
    plan.beds.squeeze();
    plan.workstations.squeeze();
    plan.mobs.squeeze();
}

} // namespace

QcVillagePlanner::QcVillagePlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyles &styles,
                                   quint64 seed)
    : m_terrain(terrain)
    , m_content(content)
    , m_styles(styles)
    , m_settings(content.villageSettings())
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

void QcVillagePlanner::chooseRoads(QcVillageDraft &draft)
{
    // Indices below the variant sequence's (which starts at 0).
    const QcVillageStyle::Road &road =
        QcVillageRoads::choose(draft.style->roads, qcPositionRandom(draft.seeds.variant, draft.region.x, -1, draft.region.z));
    const QcVillageStyle::Road &branch =
        QcVillageRoads::choose(draft.style->branchRoads, qcPositionRandom(draft.seeds.variant, draft.region.x, -2, draft.region.z));
    draft.plan.pathBlock = road.block;
    draft.plan.branchPathBlock = branch.block;
    draft.pathStep = road.step;
    draft.branchStep = branch.step;
}

const QcVillageStyle *QcVillagePlanner::styleAt(int x, int z) const
{
    const QcTerrainColumn column = m_terrain.column(x, z);
    if (column.landFactor < 0.99 || column.overhang >= 1.0) {
        return nullptr;
    }
    const QcBiomeDefinition *biome = m_content.biome(m_terrain.biomeName(column.biome));
    const QcVillageStyle *style = biome ? m_styles.forBiome(*biome) : nullptr;
    // A style over water stands on wet ground too, one on land only above the sea level.
    if (!style || (!style->overWater && column.height - 1 <= m_terrain.seaLevel())) {
        return nullptr;
    }
    return style;
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
            const int height = m_terrain.column(x + dx, z + dz).height;
            low = std::min(low, height);
            high = std::max(high, height);
        }
    }
    return high - low <= m_settings.maxHeightSpread;
}

std::optional<int> QcVillagePlanner::wetAround(const QcVillageStyle &style, int x, int z) const
{
    const int reach = m_settings.radius / 2 / flatnessStep * flatnessStep;
    return QcBoardwalkPlanner::wetAround(m_terrain, style, {x, z}, reach, flatnessStep);
}

void QcVillagePlanner::chooseCentre(QcVillageDraft &draft) const
{
    // The first of a few places in the region that is flat land of a biome with villages. Centres
    // keep a quarter of the region to its edges, so neighbouring villages lie half a region apart.
    // Once a place over water fits, the later places are tried too and the wettest one wins: a
    // village over water stands where its biome has the most water.
    const int margin = m_settings.radius + freeMargin(m_settings);
    const int room = std::max(1, m_spacing - 2 * margin);
    const int regionX = draft.region.x;
    const int regionZ = draft.region.z;
    int wettest = -1;
    for (int attempt = 0; attempt < m_settings.attempts && (!draft.style || draft.style->overWater); ++attempt) {
        const int x = regionX * m_spacing + margin + int(qcPositionRandom(m_seeds.layout, regionX, -1 - 2 * attempt, regionZ) * room);
        const int z = regionZ * m_spacing + margin + int(qcPositionRandom(m_seeds.layout, regionX, -2 - 2 * attempt, regionZ) * room);
        const QcVillageStyle *style = styleAt(x, z);
        if (!style || (draft.style && !style->overWater)) {
            continue;
        }
        const std::optional<int> wet = style->overWater ? wetAround(*style, x, z) : std::nullopt;
        const bool fits = style->overWater ? wet && *wet > wettest : isFlatAround(*style, x, z);
        if (fits) {
            draft.style = style;
            draft.centreX = x;
            draft.centreZ = z;
            wettest = wet.value_or(-1);
        }
    }
}

std::shared_ptr<const QcVillagePlan> QcVillagePlanner::plan(int regionX, int regionZ) const
{
    if (qcPositionRandom(m_seeds.layout, regionX, 0, regionZ) >= m_settings.chance || m_styles.isEmpty()) {
        return nullptr;
    }
    QcVillageDraft draft(m_seeds, {regionX, regionZ, m_spacing, m_settings.radius});
    chooseCentre(draft);
    if (!draft.style) {
        return nullptr;
    }

    startPlan(draft);
    m_roads.chooseLayout(draft);
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

    coverArea(draft);
    squeeze(draft.plan);
    return std::make_shared<const QcVillagePlan>(std::move(draft.plan));
}

void QcVillagePlanner::startPlan(QcVillageDraft &draft)
{
    QcVillagePlan &plan = draft.plan;
    plan.id = QcVillagePlan::regionId(draft.region.x, draft.region.z);
    plan.style = draft.style->name;
    chooseRoads(draft);
    plan.foundation = draft.style->foundation;
    if (draft.style->overWater) {
        plan.stiltPost = draft.style->stilts.post;
        plan.stiltSpacing = draft.style->stilts.spacing;
    }
    draft.counts.fill(0, draft.style->pieces.size());
}

void QcVillagePlanner::coverArea(QcVillageDraft &draft)
{
    QcVillagePlan &plan = draft.plan;
    plan.minX = plan.maxX = draft.centreX;
    plan.minZ = plan.maxZ = draft.centreZ;
    const QList<QcVillageDraft::Rect> covered = draft.pieces + draft.roads;
    for (const QcVillageDraft::Rect rect : covered) {
        plan.minX = std::min(plan.minX, rect.x0);
        plan.minZ = std::min(plan.minZ, rect.z0);
        plan.maxX = std::max(plan.maxX, rect.x1);
        plan.maxZ = std::max(plan.maxZ, rect.z1);
    }
    if (draft.style->overWater && draft.style->wetland.bottom != qcAirBlockId) {
        QcBoardwalkPlanner::planWetland(draft);
    }
}
