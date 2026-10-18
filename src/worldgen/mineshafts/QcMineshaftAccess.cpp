#include "worldgen/mineshafts/QcMineshaftAccess.h"

#include "worldgen/mineshafts/QcMineshaftSite.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <algorithm>
#include <cstdlib>

namespace {

// Purposes of the random values.
enum Purpose : int {
    RoomChance = 1,
    RoomPick,
    Entrance,
};

// The side the ladder of the entrance hangs on: west, against the central room's wall.
constexpr int ladderSide = 3;
// Blocks kept free of villages around an entrance.
constexpr int villageMargin = 8;

} // namespace

QcMineshaftAccess::QcMineshaftAccess(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcMineshaftSettings &settings,
                                     const QcMineshaftBlocks &blocks)
    : m_terrain(terrain)
    , m_content(content)
    , m_settings(settings)
    , m_blocks(blocks)
{
    // Structures that do not fit were reported when the content was loaded (QcSpawnerContent).
    for (const QcStructureDefinition &definition : content.structures()) {
        if (definition.mineshaftWeight <= 0 || !settings.fitsRoom(definition.sizeX, definition.sizeY, definition.sizeZ)) {
            continue;
        }
        if (QcResult<QcStructure> structure = QcStructure::fromDefinition(definition, content)) {
            m_rooms.append({std::make_shared<const QcStructure>(std::move(*structure)), definition.mineshaftWeight});
            m_totalWeight += definition.mineshaftWeight;
        }
    }
}

void QcMineshaftAccess::addRooms(QcMineshaftPlan &plan, const QcMineshaftNetwork &network, QcMineshaftSite &site, const Random &random) const
{
    if (m_rooms.isEmpty()) {
        return;
    }
    for (qsizetype i = 1; i < plan.nodes.size() && plan.rooms.size() < m_settings.maxRooms; ++i) {
        const int node = int(i);
        const auto link = std::ranges::find_if(network.links(), [node](QcMineshaftNetwork::Link l) { return l.from == node || l.to == node; });
        if (network.degree(node) != 1 || link->stairs || random(RoomChance, node) >= m_settings.roomChance) {
            continue;
        }
        const QcMineshaftPlan::Node &neighbour = plan.nodes[link->from == node ? link->to : link->from];
        const QcMineshaftPlan::Node &end = plan.nodes[node];
        // No pillar of another level may pass through the room, nor run into its floor.
        if (crossedByPillars(network, end)) {
            continue;
        }
        const int turns = QcMineshaftBlocks::sideOf(neighbour.x - end.x, neighbour.z - end.z);
        if (std::optional<QcStructurePlacement> room = roomAt(end, turns, site, random(RoomPick, node))) {
            plan.rooms.append({node, std::move(*room)});
        }
    }
}

bool QcMineshaftAccess::crossedByPillars(const QcMineshaftNetwork &network, const QcMineshaftPlan::Node &node)
{
    const QcMineshaftNetwork::Point point{node.x, node.z, node.level};
    bool bridged = node.bridge || network.bridgedElsewhere(QcMineshaftNetwork::elementKey(point), node.level);
    for (const auto &[dx, dz] : {std::pair{0, -1}, {1, 0}, {0, 1}, {-1, 0}}) {
        bridged = bridged || network.bridgedElsewhere(QcMineshaftNetwork::elementKey(point, {node.x + dx, node.z + dz, node.level}), node.level);
    }
    return bridged;
}

std::optional<QcStructurePlacement> QcMineshaftAccess::roomAt(const QcMineshaftPlan::Node &at, int turns, QcMineshaftSite &site, double pick) const
{
    int left = int(pick * m_totalWeight);
    const auto chosen = std::ranges::find_if(m_rooms, [&left](const RoomKind &kind) {
        left -= kind.weight;
        return left < 0;
    });
    const RoomKind &kind = chosen != m_rooms.end() ? *chosen : m_rooms.constLast();
    const QcStructure &structure = *kind.structure;

    // The door (the middle of the structure's north side) faces the corridor.
    const int sizeX = turns % 2 == 0 ? structure.sizeX() : structure.sizeZ();
    const int sizeZ = turns % 2 == 0 ? structure.sizeZ() : structure.sizeX();
    if (!site.roomFits(at, std::max(sizeX, sizeZ) / 2, structure.sizeY() - 1)) {
        return std::nullopt;
    }
    const QcBlockPos origin = at.centre.offset(-sizeX / 2, -1, -sizeZ / 2);
    return QcStructurePlacement(kind.structure, origin, turns, m_content);
}

std::optional<QcBlockPos> QcMineshaftAccess::surfaceAt(QcMineshaftSite &site, int x, int z, int bottom) const
{
    // The ground's top block, overhangs included (the lining closes what is open below it).
    const int top = m_terrain.groundTop(x, z, site.column(x, z));
    if (top <= m_terrain.seaLevel() || top < bottom + 2 || site.cave({x, top, z})) {
        return std::nullopt;
    }
    const std::shared_ptr<const QcVillagePlan> village = m_terrain.villageFor(QcBlockPos{x, 0, z}.chunk());
    if (village && village->covers(x, z, villageMargin)) {
        return std::nullopt;
    }
    if (!level(site, {x, top, z})) {
        return std::nullopt;
    }
    return QcBlockPos{x, top, z};
}

bool QcMineshaftAccess::level(QcMineshaftSite &site, QcBlockPos top) const
{
    // On a slope the headframe would float or bury its way out: the ground under it lies within one
    // block of the shaft's top, the ring around it at most one higher (one steps up onto it, or
    // drops down).
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int ground = m_terrain.groundTop(top.x + dx, top.z + dz, site.column(top.x + dx, top.z + dz));
            const bool under = std::abs(dx) <= 1 && std::abs(dz) <= 1;
            if (ground > top.y + 1 || (under && ground < top.y - 1)) {
                return false;
            }
        }
    }
    return true;
}

void QcMineshaftAccess::addEntrance(QcMineshaftPlan &plan, QcMineshaftSite &site, const QcMineshaftFrame &frame, const Random &random) const
{
    // In the room's north west corner, the ladder against its west wall.
    const QcMineshaftPlan::Node &room = plan.nodes.constFirst();
    const int x = frame.centre.x - room.half;
    const int z = frame.centre.z - room.half;
    const QString biome = m_terrain.biomeName(site.column(x, z).biome);
    if (random(Entrance, 0) >= m_settings.entranceChanceIn(biome)) {
        return;
    }
    const int roof = room.centre.y + room.height;
    const std::optional<QcBlockPos> top = surfaceAt(site, x, z, roof);
    if (!top || !site.clearOfDungeons({x - 1, room.centre.y, z - 1}, top->offset(1, 4, 1))) {
        return;
    }

    QcMineshaftPlan::Entrance entrance{*top, {}, {}, {}};
    for (int y = room.centre.y; y <= top->y + 2; ++y) {
        entrance.blocks.append({{x, y, z}, m_blocks.ladders[ladderSide]});
        entrance.lining.append({x - 1, y, z});
        if (y >= roof) {
            entrance.shaft.append({x, y, z});
        }
        if (y >= roof && y < top->y) {
            entrance.lining.append({x + 1, y, z});
            entrance.lining.append({x, y, z - 1});
            entrance.lining.append({x, y, z + 1});
        }
    }
    addHeadframe(entrance);
    plan.entrance = std::move(entrance);
}

void QcMineshaftAccess::addHeadframe(QcMineshaftPlan::Entrance &entrance) const
{
    const QcBlockPos top = entrance.top;
    // A plank ring around the shaft's top, posts at its corners and a roof above; the ladder goes on
    // two cells above the ground against the lining behind it.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dz != 0) {
                entrance.blocks.append({top.offset(dx, 0, dz), m_blocks.planks});
            }
            entrance.blocks.append({top.offset(dx, 4, dz), m_blocks.planks});
        }
    }
    for (const auto &[dx, dz] : {std::pair{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}) {
        for (int dy = 1; dy <= 3; ++dy) {
            entrance.blocks.append({top.offset(dx, dy, dz), m_blocks.post});
        }
    }
}
