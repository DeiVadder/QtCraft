#include "worldgen/mineshafts/QcMineshaftAccess.h"

#include "worldgen/mineshafts/QcMineshaftSite.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cstdlib>

namespace {

// Purposes of the random values.
enum Purpose : int {
    RoomChance = 1,
    RoomPick,
    Entrance,
    EntranceKind,
};

// The side the ladder of the entrance hangs on: west, against the central room's wall.
constexpr int ladderSide = 3;
// Places (a node's side) tried for an inclined entrance at most.
constexpr int maxInclineTries = 16;
// Blocks kept free of villages and surface structures around an entrance.
constexpr int villageMargin = 8;

} // namespace

QcMineshaftAccess::QcMineshaftAccess(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcMineshaftSettings &settings,
                                     const QcMineshaftBlocks &blocks)
    : m_terrain(terrain)
    , m_content(content)
    , m_settings(settings)
    , m_blocks(blocks)
    , m_incline(terrain, settings, blocks)
{
    // Structures that do not fit were reported when the content was loaded (QcSpawnerContent).
    for (const QcStructureDefinition &definition : content.worldgen().structures()) {
        if (definition.mineshaftWeight <= 0 || !settings.fitsRoom(definition.sizeX, definition.sizeY, definition.sizeZ)) {
            continue;
        }
        if (QcStructure::Loaded structure = QcStructure::fromDefinition(definition, content)) {
            m_rooms.append({std::make_shared<const QcStructure>(std::move(*structure)), definition.mineshaftWeight});
            m_totalWeight += definition.mineshaftWeight;
        }
    }
}

QList<QcMineshaftPlan::Room> QcMineshaftAccess::roomsOf(const QcMineshaftPlan &plan, const QcMineshaftNetwork &network, QcMineshaftSite &site,
                                                    const Random &random) const
{
    QList<QcMineshaftPlan::Room> rooms;
    if (roomList().isEmpty()) {
        return rooms;
    }
    for (qsizetype i = 1; i < plan.nodes.size() && rooms.size() < m_settings.maxRooms; ++i) {
        const int node = static_cast<int>(i);
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
            rooms.append({node, std::move(*room)});
        }
    }
    return rooms;
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
    int left = static_cast<int>(pick * m_totalWeight);
    const auto chosen = std::ranges::find_if(roomList(), [&left](const RoomKind &kind) {
        left -= kind.weight;
        return left < 0;
    });
    const RoomKind &kind = chosen != roomList().end() ? *chosen : roomList().constLast();
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
    if (!site.awayFromSettlements(x, z, villageMargin)) {
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

std::optional<QcMineshaftPlan::Entrance> QcMineshaftAccess::entranceOf(const QcMineshaftPlan &plan, QcMineshaftSite &site, const QcMineshaftFrame &frame,
                                                                      const Random &random) const
{
    // The chance of the biome at the room's north west corner (where the ladder goes).
    const QcMineshaftPlan::Node &room = plan.nodes.constFirst();
    const QString biome = m_terrain.biomeName(site.column(frame.centre.x - room.half, frame.centre.z - room.half).biome);
    if (random(Entrance, 0) >= m_settings.entranceChanceIn(biome)) {
        return std::nullopt;
    }

    // The kind drawn by weight first, the other one where the first finds no place (decision E-895).
    const int total = m_settings.ladderWeight + m_settings.inclineWeight;
    const bool inclineFirst = random(EntranceKind, 0) * total < m_settings.inclineWeight;
    for (const bool incline : {inclineFirst, !inclineFirst}) {
        if ((incline ? m_settings.inclineWeight : m_settings.ladderWeight) <= 0) {
            continue;
        }
        std::optional<QcMineshaftPlan::Entrance> entrance = incline ? inclineOf(plan, site, random) : ladderOf(plan, site, frame);
        if (entrance) {
            return entrance;
        }
    }
    return std::nullopt;
}

std::optional<QcMineshaftPlan::Entrance> QcMineshaftAccess::inclineOf(const QcMineshaftPlan &plan, QcMineshaftSite &site, const Random &random) const
{
    // The sides of the central room, then those of the top level's dead ends, each in turn from a
    // random one; at most maxInclineTries places.
    const int first = std::min(3, static_cast<int>(random(EntranceKind, 1) * 4.0));
    const bool rails = random(EntranceKind, 2) < m_settings.railChance;
    const QList<int> ends = deadEndsOnTop(plan);
    const int firstEnd = ends.isEmpty() ? 0 : std::min(static_cast<int>(ends.size()) - 1, static_cast<int>(random(EntranceKind, 3) * ends.size()));
    const int places = std::min(maxInclineTries, 4 * (1 + static_cast<int>(ends.size())));
    for (int place = 0; place < places; ++place) {
        const int node = place < 4 ? 0 : ends.at((firstEnd + (place - 4) / 4) % ends.size());
        const QcMineshaftIncline::Exit exit{node, (first + place) % 4};
        if (std::optional<QcMineshaftPlan::Entrance> entrance = m_incline.through(plan, site, exit, rails)) {
            return entrance;
        }
    }
    return std::nullopt;
}

QList<int> QcMineshaftAccess::deadEndsOnTop(const QcMineshaftPlan &plan)
{
    QList<int> ends;
    for (int node = 1; node < plan.nodes.size(); ++node) {
        const auto joins = std::ranges::count_if(plan.corridors, [node](const QcMineshaftPlan::Corridor &c) { return c.from == node || c.to == node; });
        if (plan.nodes.at(node).level == 0 && joins == 1) {
            ends.append(node);
        }
    }
    return ends;
}

std::optional<QcMineshaftPlan::Entrance> QcMineshaftAccess::ladderOf(const QcMineshaftPlan &plan, QcMineshaftSite &site, const QcMineshaftFrame &frame) const
{
    // In the room's north west corner, the ladder against its west wall.
    const QcMineshaftPlan::Node &room = plan.nodes.constFirst();
    const int x = frame.centre.x - room.half;
    const int z = frame.centre.z - room.half;
    const int roof = room.centre.y + room.height;
    const std::optional<QcBlockPos> top = surfaceAt(site, x, z, roof);
    if (!top || !site.clearOfDungeons({x - 1, room.centre.y, z - 1}, top->offset(1, 4, 1))) {
        return std::nullopt;
    }

    QcMineshaftPlan::Entrance entrance;
    entrance.top = *top;
    entrance.out = top->offset(0, 2, 0);
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
    entrance.blocks.append(headframe(*top));
    return entrance;
}

QList<QcMineshaftPlan::Placed> QcMineshaftAccess::headframe(QcBlockPos top) const
{
    QList<QcMineshaftPlan::Placed> blocks;
    // A plank ring around the shaft's top, posts at its corners and a roof above; the ladder goes on
    // two cells above the ground against the lining behind it.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dz != 0) {
                blocks.append({top.offset(dx, 0, dz), m_blocks.planks});
            }
            blocks.append({top.offset(dx, 4, dz), m_blocks.planks});
        }
    }
    for (const auto &[dx, dz] : {std::pair{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}) {
        for (int dy = 1; dy <= 3; ++dy) {
            blocks.append({top.offset(dx, dy, dz), m_blocks.post});
        }
    }
    return blocks;
}
