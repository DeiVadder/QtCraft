// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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
constexpr int Chance = 1;
constexpr int Levels = 2;
constexpr int CentreX = 3;
constexpr int CentreZ = 4;
constexpr int CentreY = 5;
constexpr int NetworkBase = 100;
constexpr int AccessBase = 200;
constexpr int DetailsBase = 300;

} // namespace

QcMineshaftPlanner::QcMineshaftPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed)
    : m_terrain(terrain)
    , m_settings(content.worldgen().terrainSettings().mineshafts)
    , m_seed(qcMixSeed(seed, 0x3803))
{
    if (!m_settings.enabled) {
        return;
    }
    QcMineshaftBlocks::Resolved blocks = QcMineshaftBlocks::resolve(m_settings, content);
    m_warnings = std::move(blocks.warnings);
    if (!blocks.blocks) {
        return;
    }

    m_blocks = std::move(*blocks.blocks);
    for (const QcMineshaftSettings::BiomeChance &entry : std::as_const(m_settings.entranceBiomes)) {
        if (!(std::ranges::count(content.worldgen().biomes(), entry.biome, &QcBiomeDefinition::name) > 0)) {
            m_warnings.append(u"mineshafts: 'entranceBiomes' names unknown biome '%1'"_s.arg(entry.biome));
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
    return qcPositionRandom(qcMixSeed(m_seed, static_cast<quint64>(purpose)), plan.regionX, index, plan.regionZ);
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
    const int levels = m_settings.minLevels + std::min(span - 1, static_cast<int>(random(*plan, Levels, 0) * span));
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
    plan->nodes = nodesOf(network, frame);
    plan->corridors = corridorsOf(network, frame);

    plan->rooms = m_access->roomsOf(*plan, network, site, randomFrom(AccessBase));
    QcMineshaftDetails::Furnishing furnishing = m_details->furnish(*plan, randomFrom(DetailsBase));
    plan->blocks = std::move(furnishing.blocks);
    plan->spawners = std::move(furnishing.spawners);
    plan->carts = std::move(furnishing.carts);
    plan->pillars = std::move(furnishing.pillars);
    for (const int corridor : std::as_const(furnishing.railed)) {
        plan->corridors[corridor].rails = true;
    }
    plan->entrance = m_access->entranceOf(*plan, site, frame, randomFrom(AccessBase));
    plan->bound();
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
        const int x = plan.regionX * region + extent + static_cast<int>(random(plan, CentreX, attempt) * (region - 2 * extent));
        const int z = plan.regionZ * region + extent + static_cast<int>(random(plan, CentreZ, attempt) * (region - 2 * extent));
        const int y = lowest + std::min(heights - 1, static_cast<int>(random(plan, CentreY, attempt) * heights));
        const QcMineshaftFrame frame(QcBlockPos{x, y, z}, m_settings);
        if (site.roomFits(frame.node({}), m_settings.roomHalf, m_settings.roomHeight)) {
            return QcBlockPos{x, y, z};
        }
    }
    return std::nullopt;
}

QList<QcMineshaftPlan::Node> QcMineshaftPlanner::nodesOf(const QcMineshaftNetwork &network, const QcMineshaftFrame &frame)
{
    QList<QcMineshaftPlan::Node> nodes;
    for (const QcMineshaftNetwork::Node &node : network.nodes()) {
        QcMineshaftPlan::Node placed = frame.node(node.at);
        placed.bridge = node.bridge;
        nodes.append(placed);
    }
    return nodes;
}

QList<QcMineshaftPlan::Corridor> QcMineshaftPlanner::corridorsOf(const QcMineshaftNetwork &network, const QcMineshaftFrame &frame)
{
    QList<QcMineshaftPlan::Corridor> corridors;
    for (const QcMineshaftNetwork::Link &link : network.links()) {
        QcMineshaftPlan::Corridor corridor = frame.corridor(network.nodes()[link.from].at, network.nodes()[link.to].at);
        corridor.from = link.from;
        corridor.to = link.to;
        corridor.bridge = link.bridge;
        corridors.append(corridor);
    }
    return corridors;
}
