#pragma once

#include "QcBlockAccess.h"
#include "QcItemStack.h"

#include <QList>

#include <optional>

class QcContainerManager;
class QcMachineManager;

// What a Signal Comparator reads from a block (decision E-343): the fullness of a container or
// machine with the reference formula, or a state property scaled to 0..15, as the block's data says.
class QcComparatorReadings
{
public:
    QcComparatorReadings(const QcBlockAccess &world, const QcContainerManager &containers, const QcMachineManager &machines);

    // The reference formula: 0 when every slot is empty, else 1 + 14 times the mean fullness of the
    // slots (each stack's count over its item's maxStack), rounded down.
    static int fullness(const QcContentRegistry &content, const QList<QcItemStack> &stacks);

    // The reading of the block at `pos`, or nothing when the block is not readable.
    std::optional<int> at(QcBlockPos pos) const;

private:
    int contentsAt(QcBlockPos pos) const;

    const QcBlockAccess &m_world;
    const QcContainerManager &m_containers;
    const QcMachineManager &m_machines;
};
