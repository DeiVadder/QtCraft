#include "QcLiquidFlow.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QcBlockFace, 4> sideways = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};

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

    if (m_planned.size() >= maxPlanned) {
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
    for (const QcBlockFace face : sideways) {
        const QcBlockPos side = qcNeighbour(pos, face);
        if (canFlowInto(world, side, spread)) {
            change(side, spread, context);
        }
    }
}
