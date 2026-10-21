#pragma once

#include "QcCircuitFixture.h"
#include "server/signal/QcServerHoppers.h"

// Signal machines on a running server: containers filled as a player or the world's
// setup would (comparators and hoppers around are told), items dropped at local positions and
// counted wherever they are, so tests can prove that nothing is lost or copied.
class QcMachineFixture : public QcCircuitFixture
{
    Q_OBJECT

protected:
    quint32 m_nextItemId = 900000;

    QcItemStack stack(const char *name, int count) const { return {item(name), count, 0}; }

    // Puts `stacks` into the first slots of the container or machine at local (x, y, z).
    void fill(int x, int y, int z, const QList<QcItemStack> &stacks)
    {
        const QcBlockPos pos = at(x, y, z);
        const QcBlockId block = *m_server->world().blockAt(pos);
        QVERIFY(ensureEntity(pos));
        if (m_server->blockEntities().containerSlots(block) > 0) {
            QcBlockEntities::Container *container = m_server->blockEntities().container(pos);
            for (qsizetype i = 0; i < stacks.size(); ++i) {
                container->stacks[i] = stacks[i];
            }
        } else {
            for (qsizetype i = 0; i < stacks.size(); ++i) {
                m_server->blockEntities().machine(pos)->stacks[std::size_t(i)] = stacks[i];
            }
        }
        m_server->noteContents(pos);
    }

    QList<QcItemStack> slotsAt(int x, int y, int z) const
    {
        const QcBlockPos pos = at(x, y, z);
        if (const QcBlockEntities::Container *container = m_server->blockEntities().container(pos)) {
            return container->stacks;
        }
        if (const QcBlockEntities::Machine *machine = m_server->blockEntities().machine(pos)) {
            return QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end());
        }
        return {};
    }

    // Items of a kind in the container at local (x, y, z).
    int countIn(int x, int y, int z, const char *name) const
    {
        int total = 0;
        const QList<QcItemStack> stacks = slotsAt(x, y, z);
        for (const QcItemStack &slot : stacks) {
            total += slot.item == item(name) ? slot.count : 0;
        }
        return total;
    }

    // Items of a kind anywhere: containers, machines, item entities, the player.
    int everywhere(const char *name) const
    {
        const QcItemId wanted = item(name);
        int total = droppedCount(wanted) + countOf(wanted);
        const QList<QcBlockEntities::Container> containers = allContainers();
        for (const QcBlockEntities::Container &container : containers) {
            for (const QcItemStack &slot : container.stacks) {
                total += slot.item == wanted ? slot.count : 0;
            }
        }
        const QList<QcBlockEntities::Machine> machines = allMachines();
        for (const QcBlockEntities::Machine &machine : machines) {
            for (const QcItemStack &slot : machine.stacks) {
                total += slot.item == wanted ? slot.count : 0;
            }
        }
        return total;
    }

    // An item entity at local (x + fx, y + fy, z + fz), fx and fz measured in the turned frame
    // (0.5 is the middle of the cell in every turn).
    quint32 drop(double x, double y, double z, const QcItemStack &dropped)
    {
        const QcBlockPos cell = at(int(std::floor(x)), int(std::floor(y)), int(std::floor(z)));
        QcItemEntityManager::Drop entity;
        entity.stack = dropped;
        entity.position = {cell.x + 0.5, cell.y + (y - std::floor(y)), cell.z + 0.5};
        entity.pickupDelay = 0.0;
        return m_server->items().spawn(entity, m_nextItemId++);
    }

    const QcServerHoppers::Stats &hopperStats() const { return m_server->systems().circuits.hoppers().lastTick(); }
};
