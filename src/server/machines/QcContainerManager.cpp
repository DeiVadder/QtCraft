#include "QcContainerManager.h"

QcContainerManager::QcContainerManager(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

int QcContainerManager::slotsOf(QcBlockId block) const
{
    return m_content->isValidBlock(block) ? m_content->block(block).containerSlots : 0;
}

QcContainerManager::Container *QcContainerManager::find(QcBlockPos pos)
{
    const auto it = m_containers.find(pos);
    return it == m_containers.end() ? nullptr : &*it;
}

const QcContainerManager::Container *QcContainerManager::find(QcBlockPos pos) const
{
    const auto it = m_containers.constFind(pos);
    return it == m_containers.constEnd() ? nullptr : &*it;
}

bool QcContainerManager::add(QcBlockPos pos, int slotCount)
{
    if (find(pos)) {
        return true;
    }
    if (isFull() || slotCount <= 0) {
        return false;
    }
    m_containers.insert(pos, {pos, QList<QcItemStack>(slotCount)});
    return true;
}

QList<QcItemStack> QcContainerManager::resize(QcBlockPos pos, int slotCount)
{
    Container *container = find(pos);
    if (!container || slotCount <= 0) {
        return {};
    }

    QList<QcItemStack> cut;
    if (container->stacks.size() > slotCount) {
        cut = container->stacks.mid(slotCount);
    }
    container->stacks.resize(slotCount);
    return cut;
}

QcContainerManager::Container QcContainerManager::remove(QcBlockPos pos)
{
    Container container = m_containers.take(pos);
    container.pos = pos;
    return container;
}

bool QcContainerManager::relocate(QcBlockPos from, QcBlockPos to)
{
    if (!m_containers.contains(from) || m_containers.contains(to)) {
        return false;
    }

    Container container = m_containers.take(from);
    container.pos = to;
    m_containers.insert(to, container);
    return true;
}

void QcContainerManager::restore(const QList<Container> &containers)
{
    m_containers.clear();
    for (const Container &container : containers) {
        if (isFull()) {
            break;
        }
        if (container.stacks.isEmpty()) {
            continue;
        }
        m_containers.insert(container.pos, container);
    }
}
