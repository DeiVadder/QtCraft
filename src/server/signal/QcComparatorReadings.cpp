#include "QcComparatorReadings.h"

#include "QcBlockEntities.h"

#include <cmath>

QcComparatorReadings::QcComparatorReadings(const QcBlockAccess &world, const QcBlockEntities &entities)
    : m_world(world)
    , m_entities(entities)
{
}

int QcComparatorReadings::fullness(const QcContentRegistry &content, const QList<QcItemStack> &stacks)
{
    if (stacks.isEmpty()) {
        return 0;
    }

    double sum = 0.0;
    bool any = false;
    for (const QcItemStack &stack : stacks) {
        if (stack.isEmpty() || !content.isValidItem(stack.item)) {
            continue;
        }
        any = true;
        sum += double(stack.count) / double(content.item(stack.item).maxStack);
    }
    if (!any) {
        return 0;
    }
    return std::min(15, int(std::floor(1.0 + sum / double(stacks.size()) * 14.0)));
}

std::optional<int> QcComparatorReadings::at(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_world.blockAt(pos);
    const QcContentRegistry &content = m_world.content();
    if (!block || !content.isValidBlock(*block)) {
        return std::nullopt;
    }

    const QcSignalDefinition &signal = content.block(*block).signal;
    switch (signal.reading) {
    case QcComparatorReading::Contents:
        return contentsAt(pos);
    case QcComparatorReading::Property: {
        const QcBlockProperty *property = content.block(*block).property(signal.readProperty);
        if (!property || property->values.size() < 2) {
            return 0;
        }
        const auto index = property->values.indexOf(content.property(*block, signal.readProperty));
        return int(std::max<qsizetype>(0, index) * 15 / (property->values.size() - 1));
    }
    case QcComparatorReading::None:
        break;
    }
    return std::nullopt;
}

int QcComparatorReadings::contentsAt(QcBlockPos pos) const
{
    const QcContentRegistry &content = m_world.content();
    if (const QcBlockEntities::Container *container = m_entities.container(pos)) {
        return fullness(content, container->stacks);
    }
    if (const QcBlockEntities::Machine *machine = m_entities.machine(pos)) {
        return fullness(content, QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end()));
    }
    // A container nobody opened yet is empty.
    return 0;
}
