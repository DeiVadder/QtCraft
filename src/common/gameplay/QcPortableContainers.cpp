#include "common/gameplay/QcPortableContainers.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

namespace QcPortableContainers {

bool isPortable(const QcContentRegistry &content, QcItemId item)
{
    return content.isValidItem(item) && content.item(item).portableContainer;
}

bool mayHold(const QcContentRegistry &content, const QcItemStack &stack)
{
    return stack.isEmpty() || (!stack.hasContents() && !isPortable(content, stack.item));
}

bool mayHoldAll(const QcContentRegistry &content, const QList<QcItemStack> &stacks)
{
    return std::ranges::all_of(stacks, [&content](const QcItemStack &stack) { return mayHold(content, stack); });
}

QList<QcItemStack> slotsOf(const QcContentRegistry &content, const QcItemStack &stack, int slotCount)
{
    QList<QcItemStack> result = stack.contents().mid(0, slotCount);
    for (QcItemStack &slot : result) {
        if (!mayHold(content, slot)) {
            slot = {};
        }
    }
    result.resize(std::max(0, slotCount));
    return result;
}

QList<QcItemStack> leftOver(const QcContentRegistry &content, const QcItemStack &stack, int slotCount)
{
    QList<QcItemStack> left;
    const QList<QcItemStack> &contents = stack.contents();
    for (qsizetype i = 0; i < contents.size(); ++i) {
        if (!contents[i].isEmpty() && (i >= slotCount || !mayHold(content, contents[i]))) {
            left.append(contents[i]);
        }
    }
    return left;
}

} // namespace QcPortableContainers
