#include "QcLiquidFlow.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QcBlockFace, 4> sideways = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};
constexpr int sideCount = int(sideways.size());

// Cells the liquid `spread` may run through: those it could flow into at any level (air, replaceable
// and washable blocks, flowing parts of the same liquid).
bool isPassable(const QcBlockAccess &world, QcBlockPos pos, QcBlockId spread)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const bool sameFlowing = block && content.isValidBlock(*block) && content.isSameBlock(*block, spread)
                             && !QcLiquidFlow::isSource(content, *block);
    return pos.isInsideWorldHeight() && (sameFlowing || QcLiquidFlow::canReplace(content, block, spread));
}

// Breadth-first search over the cells around a spreading liquid at the same height for the nearest
// drops (cells the liquid can fall out of). Each cell remembers through which of the four first
// steps (bit per entry of `sideways`) it is reached on a shortest way.
class DropSearch
{
public:
    // Looks `radius` blocks (at most QcLiquidDefinition::maxDropSearch) around `centre`.
    DropSearch(const QcBlockAccess &world, QcBlockPos centre, QcBlockId spread, int radius)
        : m_world(world)
        , m_content(world.content())
        , m_centre(centre)
        , m_spread(spread)
        , m_falling(QcLiquidFlow::stateWith(m_content, spread, 1, true))
        , m_radius(std::min(radius, QcLiquidDefinition::maxDropSearch))
    {
    }

    // Bits of the first steps that lead to a nearest drop; 0 without a drop within the radius.
    int nearestDropSteps(int openSteps)
    {
        QList<QcBlockPos> frontier;
        for (int i = 0; i < sideCount; ++i) {
            if (openSteps & (1 << i)) {
                const QcBlockPos side = qcNeighbour(m_centre, sideways[std::size_t(i)]);
                cell(side) = {1, 1 << i};
                frontier.append(side);
            }
        }
        cell(m_centre).distance = blocked;

        for (int distance = 1; !frontier.isEmpty(); ++distance) {
            if (const int steps = stepsToDrops(frontier)) {
                return steps;
            }
            if (distance == m_radius) {
                break;
            }
            frontier = expand(frontier, distance + 1);
        }
        return 0;
    }

private:
    static constexpr int size = 2 * QcLiquidDefinition::maxDropSearch + 1;
    static constexpr int blocked = -1;

    struct Cell
    {
        // 0: not looked at yet, `blocked`, or the walking distance from the centre.
        int distance = 0;
        // Bits of the first steps of the shortest ways here.
        int steps = 0;
    };

    Cell &cell(QcBlockPos pos)
    {
        const int index = (pos.z - m_centre.z + size / 2) * size + pos.x - m_centre.x + size / 2;
        return m_cells[std::size_t(index)];
    }

    int stepsToDrops(const QList<QcBlockPos> &frontier)
    {
        int steps = 0;
        for (const QcBlockPos &pos : frontier) {
            if (isDrop(pos.offset(0, -1, 0))) {
                steps |= cell(pos).steps;
            }
        }
        return steps;
    }

    // The ring of open cells at `distance`; a cell reached from several cells of the ring before
    // joins their steps. Every cell is checked for passing at most once.
    QList<QcBlockPos> expand(const QList<QcBlockPos> &frontier, int distance)
    {
        QList<QcBlockPos> next;
        for (const QcBlockPos &pos : frontier) {
            for (const QcBlockFace face : sideways) {
                const QcBlockPos neighbour = qcNeighbour(pos, face);
                if (std::max(std::abs(neighbour.x - m_centre.x), std::abs(neighbour.z - m_centre.z)) > m_radius) {
                    continue;
                }

                Cell &reached = cell(neighbour);
                if (reached.distance == 0) {
                    reached.distance = isPassable(m_world, neighbour, m_spread) ? distance : blocked;
                    next.append(neighbour);
                }
                if (reached.distance == distance) {
                    reached.steps |= cell(pos).steps;
                }
            }
        }

        next.removeIf([this](QcBlockPos pos) { return cell(pos).distance == blocked; });
        return next;
    }

    // A cell below that the liquid can fall into or that already holds it (a filled hole stays a
    // drop, so the stream keeps running there).
    bool isDrop(QcBlockPos below) const
    {
        const std::optional<QcBlockId> block = m_world.blockAt(below);
        const bool holdsLiquid = block && m_content.isValidBlock(*block) && m_content.isSameBlock(*block, m_spread);
        return holdsLiquid || QcLiquidFlow::canFlowInto(m_world, below, m_falling);
    }

    const QcBlockAccess &m_world;
    const QcContentRegistry &m_content;
    QcBlockPos m_centre;
    QcBlockId m_spread;
    QcBlockId m_falling;
    int m_radius;
    std::array<Cell, std::size_t(size * size)> m_cells{};
};

} // namespace

QcLiquidFlow::QcLiquidFlow(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

bool QcLiquidFlow::flows(const QcContentRegistry &content, QcBlockId id)
{
    if (!content.isValidBlock(id)) {
        return false;
    }

    const QcBlockDefinition &definition = content.block(id);
    return definition.liquid && definition.flow.flows();
}

int QcLiquidFlow::level(const QcContentRegistry &content, QcBlockId id)
{
    return content.property(id, u"level"_s).toInt();
}

bool QcLiquidFlow::isFalling(const QcContentRegistry &content, QcBlockId id)
{
    return content.property(id, u"falling"_s) == u"true"_s;
}

bool QcLiquidFlow::isSource(const QcContentRegistry &content, QcBlockId id)
{
    return level(content, id) == 0 && !isFalling(content, id);
}

QcVec3 QcLiquidFlow::current(const QcBlockAccess &world, QcBlockPos pos)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> here = world.blockAt(pos);
    if (!here || !content.isValidBlock(*here) || !content.block(*here).liquid) {
        return {};
    }

    // Depth of the surface: 8 for sources and falling liquid, less the further the flow ran.
    const auto depthOf = [&content](QcBlockId id) { return isFalling(content, id) ? 8 : 8 - level(content, id); };
    const int depth = depthOf(*here);
    QcVec3 push;
    for (const QcBlockFace side : sideways) {
        const QcBlockPos next = qcNeighbour(pos, side);
        const std::optional<QcBlockId> block = world.blockAt(next);
        if (!block || !content.isValidBlock(*block)) {
            continue;
        }

        const bool sameLiquid = content.isSameBlock(*block, *here);
        const bool open = !sameLiquid && !content.block(*block).liquid && content.state(*block).collision.isEmpty();
        if (!sameLiquid && !open) {
            continue;
        }

        const QcBlockPos step = qcNeighbour({0, 0, 0}, side);
        push = push + QcVec3{double(step.x), 0.0, double(step.z)} * double(depth - (sameLiquid ? depthOf(*block) : 0));
    }
    return push.length() > 1e-9 ? push.normalized() : QcVec3{};
}

QcBlockId QcLiquidFlow::stateWith(const QcContentRegistry &content, QcBlockId id, int level, bool falling)
{
    QcBlockId state = content.stateTable().firstState(content.state(id).type);
    state = content.withProperty(state, u"level"_s, QString::number(level));
    return content.withProperty(state, u"falling"_s, falling ? u"true"_s : u"false"_s);
}

std::optional<QcBlockId> QcLiquidFlow::reaction(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    if (!flows(content, id) || content.block(id).flow.reactions.isEmpty()) {
        return std::nullopt;
    }

    const QcBlockDefinition &definition = content.block(id);
    for (const QcBlockFace face : qcAllBlockFaces) {
        const std::optional<QcBlockId> neighbour = world.blockAt(qcNeighbour(pos, face));
        if (!neighbour || !content.isValidBlock(*neighbour) || !content.block(*neighbour).liquid || content.isSameBlock(*neighbour, id)) {
            continue;
        }

        const QString &other = content.block(*neighbour).name;
        const auto it = std::ranges::find(definition.flow.reactions, other, &QcLiquidReaction::with);
        if (it == definition.flow.reactions.end()) {
            continue;
        }

        if (const std::optional<QcBlockId> result = content.blockId(isSource(content, id) ? it->source : it->flowing)) {
            return result;
        }
    }
    return std::nullopt;
}

std::optional<QcBlockId> QcLiquidFlow::fedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    const QcLiquidDefinition &flow = content.block(id).flow;
    const QcBlockPos above = pos.offset(0, 1, 0);

    if (above.isInsideWorldHeight()) {
        const std::optional<QcBlockId> top = world.blockAt(above);
        if (!top) {
            return std::nullopt;
        }
        if (content.isValidBlock(*top) && content.isSameBlock(*top, id)) {
            return stateWith(content, id, 1, true);
        }
    }

    int best = flow.distance + 1;
    int sources = 0;

    for (const QcBlockFace face : sideways) {
        const std::optional<QcBlockId> neighbour = world.blockAt(qcNeighbour(pos, face));
        if (!neighbour) {
            return std::nullopt;
        }

        if (!content.isValidBlock(*neighbour) || !content.isSameBlock(*neighbour, id)) {
            continue;
        }

        // A falling part spreads from where it lands like a source.
        const int effective = isFalling(content, *neighbour) ? 0 : level(content, *neighbour);
        best = std::min(best, effective + 1);
        sources += isSource(content, *neighbour) ? 1 : 0;
    }

    if (flow.infinite && sources >= 2) {
        const std::optional<QcBlockId> below = world.blockAt(pos.offset(0, -1, 0));
        const bool ground = pos.y == 0
                            || (below && content.isValidBlock(*below)
                                && (content.block(*below).solid || (content.isSameBlock(*below, id) && isSource(content, *below))));
        if (ground) {
            return stateWith(content, id, 0, false);
        }
    }

    if (best > flow.distance) {
        return qcAirBlockId;
    }
    return stateWith(content, id, best, false);
}

bool QcLiquidFlow::canFlowInto(const QcBlockAccess &world, QcBlockPos pos, QcBlockId state)
{
    return pos.isInsideWorldHeight() && canReplace(world.content(), world.blockAt(pos), state);
}

bool QcLiquidFlow::canReplace(const QcContentRegistry &content, std::optional<QcBlockId> block, QcBlockId state)
{
    if (!block || !content.isValidBlock(*block)) {
        return false;
    }
    if (*block == qcAirBlockId) {
        return true;
    }

    const QcBlockDefinition &definition = content.block(*block);
    if (definition.liquid) {
        if (!content.isSameBlock(*block, state) || isSource(content, *block)) {
            return false;
        }

        if (isFalling(content, state)) {
            return !isFalling(content, *block);
        }

        return !isFalling(content, *block) && level(content, *block) > level(content, state);
    }

    return definition.replaceable || definition.washable;
}

bool QcLiquidFlow::schedule(const QcBlockAccess &world, QcBlockPos pos, quint64 now)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block || !flows(*m_content, *block) || m_planned.contains(pos)) {
        return false;
    }

    if (m_planned.size() >= m_maxPlanned) {
        ++m_dropped;
        return false;
    }

    const quint64 due = now + quint64(m_content->block(*block).flow.ticks);
    m_planned.insert(pos, due);
    m_due[due].append(pos);
    return true;
}

QcLiquidFlow::TickResult QcLiquidFlow::tick(quint64 now, const QcBlockAccess &world, const SetFunction &set, int budget)
{
    TickResult result;
    const StepContext context{now, world, set, result};

    while (!m_due.empty() && m_due.begin()->first <= now && result.steps < budget) {
        QList<QcBlockPos> &positions = m_due.begin()->second;
        if (positions.isEmpty()) {
            m_due.erase(m_due.begin());
            continue;
        }

        const QcBlockPos pos = positions.takeFirst();
        m_planned.remove(pos);
        ++result.steps;
        step(pos, context);
    }

    return result;
}

void QcLiquidFlow::clear()
{
    m_due.clear();
    m_planned.clear();
}

void QcLiquidFlow::forgetChunk(QcChunkPos chunk)
{
    const auto inChunk = [chunk](QcBlockPos pos) { return pos.chunk() == chunk; };
    m_planned.removeIf([&inChunk](const auto &planned) { return inChunk(planned.key()); });
    std::erase_if(m_due, [&inChunk](auto &due) {
        due.second.removeIf(inChunk);
        return due.second.isEmpty();
    });
}

QHash<QcChunkPos, QList<QcBlockPos>> QcLiquidFlow::takeChunks(const QSet<QcChunkPos> &chunks)
{
    QHash<QcChunkPos, QList<QcBlockPos>> taken;
    if (m_planned.isEmpty()) {
        return taken;
    }
    // In the order they are due, so they come back in the same order.
    std::erase_if(m_due, [&](auto &due) {
        due.second.removeIf([&](QcBlockPos pos) {
            if (!chunks.contains(pos.chunk())) {
                return false;
            }
            taken[pos.chunk()].append(pos);
            return true;
        });
        return due.second.isEmpty();
    });
    m_planned.removeIf([&chunks](const auto &planned) { return chunks.contains(planned.key().chunk()); });
    return taken;
}

void QcLiquidFlow::change(QcBlockPos pos, QcBlockId block, const StepContext &context)
{
    context.set(pos, block);
    ++context.result.changes;
    schedule(context.world, pos, context.now);

    for (const QcBlockFace face : qcAllBlockFaces) {
        schedule(context.world, qcNeighbour(pos, face), context.now);
    }
}

void QcLiquidFlow::step(QcBlockPos pos, const StepContext &context)
{
    const QcContentRegistry &content = *m_content;
    const QcBlockAccess &world = context.world;
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block) {
        context.result.unloaded.append(pos);
        return;
    }

    if (!flows(content, *block)) {
        return;
    }

    if (const std::optional<QcBlockId> turned = reaction(world, pos, *block)) {
        change(pos, *turned, context);
        return;
    }

    QcBlockId current = *block;
    if (!isSource(content, current)) {
        // Waits for the border update that comes when the missing chunk loads.
        const std::optional<QcBlockId> fed = fedState(world, pos, current);
        if (!fed) {
            return;
        }

        if (*fed != current) {
            change(pos, *fed, context);
            if (!flows(content, *fed)) {
                return;
            }
            current = *fed;
        }
    }

    const QcBlockPos belowPos = pos.offset(0, -1, 0);
    const QcBlockId falling = stateWith(content, current, 1, true);
    const bool down = canFlowInto(world, belowPos, falling);
    if (down) {
        change(belowPos, falling, context);
    }

    const std::optional<QcBlockId> below = world.blockAt(belowPos);
    const bool onLiquid = below && content.isValidBlock(*below) && content.isSameBlock(*below, current);
    if (!isSource(content, current) && (down || onLiquid)) {
        return;
    }

    const int next = (isFalling(content, current) ? 0 : level(content, current)) + 1;
    if (next > content.block(current).flow.distance) {
        return;
    }

    const QcBlockId spread = stateWith(content, current, next, false);
    const QList<QcBlockFace> faces = spreadFaces(world, pos, spread);
    for (const QcBlockFace face : faces) {
        change(qcNeighbour(pos, face), spread, context);
    }
}

QList<QcBlockFace> QcLiquidFlow::spreadFaces(const QcBlockAccess &world, QcBlockPos pos, QcBlockId spread)
{
    // Sides it can flow into now, and sides the search runs through (also those that already
    // carry its flow, so an established stream keeps its direction).
    int flowable = 0;
    int passable = 0;
    for (int i = 0; i < sideCount; ++i) {
        const QcBlockPos side = qcNeighbour(pos, sideways[std::size_t(i)]);
        flowable |= canFlowInto(world, side, spread) ? 1 << i : 0;
        passable |= isPassable(world, side, spread) ? 1 << i : 0;
    }

    // Drops further than the spreading parts can still reach do not matter.
    const QcLiquidDefinition &definition = world.content().block(spread).flow;
    const int radius = std::min(definition.dropSearch, definition.distance - level(world.content(), spread) + 1);
    const bool search = flowable != 0 && radius > 0 && std::popcount(unsigned(passable)) > 1;
    const int nearest = search ? DropSearch(world, pos, spread, radius).nearestDropSteps(passable) : 0;
    const int chosen = (nearest != 0 ? nearest : passable) & flowable;

    QList<QcBlockFace> faces;
    for (int i = 0; i < sideCount; ++i) {
        if (chosen & (1 << i)) {
            faces.append(sideways[std::size_t(i)]);
        }
    }
    return faces;
}
