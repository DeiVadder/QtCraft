#pragma once

#include "QcContentRegistry.h"
#include "QcItemStack.h"

#include <QHash>
#include <QList>

#include <memory>

// Inventories of storage blocks (chests) on the server, one per block position, sized by the block's
// `containerSlots`. It is the block-entity technique of QcMachineManager without a tick: contents
// stay until the block is broken, then they drop (decision E-050).
class QcContainerManager
{
public:
    static constexpr int maxContainers = 16384;

    struct Container
    {
        QcBlockPos pos;
        QList<QcItemStack> stacks;
    };

    explicit QcContainerManager(std::shared_ptr<const QcContentRegistry> content);

    // Storage slots of a block, 0 when it stores nothing.
    int slotsOf(QcBlockId block) const;

    Container *find(QcBlockPos pos);
    const Container *find(QcBlockPos pos) const;
    // Adds an empty container of `slotCount` unless one exists (which stays as it is); false when
    // the limit is reached or `slotCount` is not positive.
    bool add(QcBlockPos pos, int slotCount);
    // Changes the size of an existing container; returns the stacks of the slots that were cut off.
    QList<QcItemStack> resize(QcBlockPos pos, int slotCount);
    // Removes the container and returns it with its contents (to be dropped).
    Container remove(QcBlockPos pos);
    // Moves the container at `from` with its contents to `to` (a piston pushed its block, decision
    // E-337); false when there is none at `from` or one at `to` already.
    bool relocate(QcBlockPos from, QcBlockPos to);
    bool isFull() const { return m_containers.size() >= maxContainers; }
    qsizetype count() const { return m_containers.size(); }
    QList<Container> containers() const { return m_containers.values(); }
    QList<QcBlockPos> positions() const { return m_containers.keys(); }
    // Replaces all containers, e.g. from a save; entries beyond the limit or without slots are left out.
    void restore(const QList<Container> &containers);

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QHash<QcBlockPos, Container> m_containers;
};
