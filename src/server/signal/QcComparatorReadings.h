#pragma once

#include "common/gameplay/QcItemStack.h"
#include "common/world/QcBlockAccess.h"

#include <QList>

#include <optional>

class QcBlockEntities;
class QcMinecartManager;

// What a Signal Comparator reads from a block (decision E-343): the fullness of a container or
// machine with the reference formula, a state property scaled to 0..15, or the fullness of a
// container cart on a detector rail (decision E-385), as the block's data says.
class QcComparatorReadings
{
public:
    QcComparatorReadings(const QcBlockAccess &world, const QcBlockEntities &entities, const QcMinecartManager &carts);

    // The reference formula: 0 when every slot is empty, else 1 + 14 times the mean fullness of the
    // slots (each stack's count over its item's maxStack), rounded down.
    static int fullness(const QcContentRegistry &content, const QList<QcItemStack> &stacks);

    // The reading of the block at `pos`, or nothing when the block is not readable.
    [[nodiscard]] std::optional<int> at(QcBlockPos pos) const;

private:
    int contentsAt(QcBlockPos pos) const;

    const QcBlockAccess &m_world;
    const QcBlockEntities &m_entities;
    const QcMinecartManager &m_carts;
};
