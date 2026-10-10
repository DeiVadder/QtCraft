#include "QcEquipment.h"

#include "QcContentRegistry.h"

QcEquipment QcEquipment::knownTo(const QcContentRegistry &content) const
{
    QcEquipment known;
    for (std::size_t slot = 0; slot < slotCount; ++slot) {
        const QcItemStack &stack = stacks[slot];
        if (!stack.isEmpty() && content.isValidItem(stack.item)) {
            known.stacks[slot] = stack;
        }
    }
    return known;
}
