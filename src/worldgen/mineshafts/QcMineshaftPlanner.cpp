#include "worldgen/mineshafts/QcMineshaftPlanner.h"

#include "worldgen/mineshafts/QcMineshaftFrame.h"
#include "worldgen/mineshafts/QcMineshaftNetwork.h"
#include "worldgen/mineshafts/QcMineshaftSite.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Purposes of the random values; the network, the access and the details count their own from
// these bases.
enum Purpose : int {
    Chance = 1,
    Levels,
    CentreX,
    CentreZ,
    CentreY,
    NetworkBase = 100,
    AccessBase = 200,
    DetailsBase = 300,
};

} // namespace

QcMineshaftPlanner::QcMineshaftPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed, QStringList *warnings)
    : m_terrain(terrain)
    , m_settings(content.terrainSettings().mineshafts)
    , m_seed(qcMixSeed(seed, 0x3803))
{
    if (!m_settings.enabled) {
        return;
    }
    const std::optional<QcMineshaftBlocks> blocks = QcMineshaftBlocks::resolve(m_settings, content, warnings);
    if (!blocks) {
        return;
    }

    m_blocks = *blocks;
    for (const QcMineshaftSettings::BiomeChance &entry : std::as_const(m_settings.entranceBiomes)) {
        if (warnings && !std::ranges::contains(content.biomes(), entry.biome, &QcBiomeDefinition::name)) {
            warnings->append(u"mineshafts: 'entranceBiomes' names unknown biome '%1'"_s.arg(entry.biome));
        }
    }
    const std::optional<QcItemId> cart = content.itemId(m_settings.cart);
    const bool carts = cart && content.item(*cart).placesCart;
    m_access = std::make_unique<QcMineshaftAccess>(terrain, content, m_settings, m_blocks);
    m_details = std::make_unique<QcMineshaftDetails>(m_settings, m_blocks, carts);
    m_enabled = true;
}

QcMineshaftPlanner::~QcMineshaftPlanner() = default;

double QcMineshaftPlanner::random(const QcMineshaftPlan &plan, int purpose, int index) const
{
    return qcPositionRandom(qcMixSeed(m_seed, quint64(purpose)), plan.regionX, index, plan.regionZ);
}

std::shared_ptr<const QcMineshaftPlan> QcMineshaftPlanner::plan(int regionX, int regionZ) const
{
    if (!m_enabled) {
        return nullptr;
    }
    auto plan = std::make_shared<QcMineshaftPlan>();
    plan->regionX = regionX;
    plan->regionZ = regionZ;
    if (random(*plan, Chance, 0) >= m_settings.chance) {
        return nullptr;
    }

    const int span = m_settings.maxLevels - m_settings.minLevels + 1;
    const int levels = m_settings.minLevels + std::min(span - 1, int(random(*plan, Levels, 0) * span));
    QcMineshaftSite site(m_terrain, m_settings, regionX, regionZ);
    const std::optional<QcBlockPos> centre = findCentre(site, *plan, levels);
    if (!centre) {
        return nullptr;
    }

    const QcMineshaftFrame frame(*centre, m_settings);
    const auto randomFrom = [this, &plan](int base) { return [this, plan, base](int purpose, int index) { return random(*plan, base + purpose, index); }; };
    QcMineshaftNetwork network(
        m_settings, randomFrom(NetworkBase), [&](QcMineshaftNetwork::Point at) { return site.node(frame.node(at)); },
        [&](QcMineshaftNetwork::Point from, QcMineshaftNetwork::Point to) { return site.corridor(frame.corridor(from, to)); });
    plan->levels = network.grow(levels, false);
    layOut(*plan, network, frame);

    m_access->addRooms(*plan, network, site, randomFrom(AccessBase));
    m_details->add(*plan, randomFrom(DetailsBase));
    m_access->addEntrance(*plan, site, frame, randomFrom(AccessBase));
    bound(*plan);
    return plan;
}

std::optional<QcBlockPos> QcMineshaftPlanner::findCentre(QcMineshaftSite &site, const QcMineshaftPlan &plan, int levels) const
{
    // The whole network stays inside the region.
    const int region = m_settings.regionSize;
    const int extent = m_settings.extent();
    const int lowest = m_settings.minY + (levels - 1) * m_settings.levelSpacing;
    const int heights = std::max(1, m_settings.maxY - lowest + 1);
    for (int attempt = 0; attempt < m_settings.attempts; ++attempt) {
        const int x = plan.regionX * region + extent + int(random(plan, CentreX, attempt) * (region - 2 * extent));
        const int z = plan.regionZ * region + extent + int(random(plan, CentreZ, attempt) * (region - 2 * extent));
        const int y = lowest + std::min(heights - 1, int(random(plan, CentreY, attempt) * heights));
        const QcMineshaftFrame frame(QcBlockPos{x, y, z}, m_settings);
        if (site.roomFits(frame.node({}), m_settings.roomHalf, m_settings.roomHeight)) {
            return QcBlockPos{x, y, z};
        }
    }
    return std::nullopt;
}

void QcMineshaftPlanner::layOut(QcMineshaftPlan &plan, const QcMineshaftNetwork &network, const QcMineshaftFrame &frame)
{
    for (const QcMineshaftNetwork::Node &node : network.nodes()) {
        QcMineshaftPlan::Node placed = frame.node(node.at);
        placed.bridge = node.bridge;
        plan.nodes.append(placed);
    }
    for (const QcMineshaftNetwork::Link &link : network.links()) {
        QcMineshaftPlan::Corridor corridor = frame.corridor(network.nodes()[link.from].at, network.nodes()[link.to].at);
        corridor.from = link.from;
        corridor.to = link.to;
        corridor.bridge = link.bridge;
        plan.corridors.append(corridor);
    }
}

void QcMineshaftPlanner::bound(QcMineshaftPlan &plan)
{
    const QcBlockPos centre = plan.nodes.constFirst().centre;
    plan.minX = plan.maxX = centre.x;
    plan.minZ = plan.maxZ = centre.z;
    const auto add = [&plan](QcBlockPos cell) {
        plan.minX = std::min(plan.minX, cell.x - 1);
        plan.minZ = std::min(plan.minZ, cell.z - 1);
        plan.maxX = std::max(plan.maxX, cell.x + 1);
        plan.maxZ = std::max(plan.maxZ, cell.z + 1);
    };
    plan.forEachOpenCell(add);
    for (const QcMineshaftPlan::Placed &placed : std::as_const(plan.blocks)) {
        add(placed.at);
    }
    if (plan.entrance) {
        for (const QcMineshaftPlan::Placed &placed : std::as_const(plan.entrance->blocks)) {
            add(placed.at);
        }
        for (const QcBlockPos &cell : std::as_const(plan.entrance->lining)) {
            add(cell);
        }
    }
    for (const QcMineshaftPlan::Room &room : std::as_const(plan.rooms)) {
        add(room.placement.origin());
        add(room.placement.origin().offset(room.placement.sizeX() - 1, 0, room.placement.sizeZ() - 1));
    }
}
