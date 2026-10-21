#include "common/gameplay/QcSlotList.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

bool QcSlotList::moveBetween(int from, int to, int count, const QcContentRegistry &content)
{
    if (from < 0 || to < 0 || from >= stacks.size() || to >= stacks.size() || from == to || count < 0) {
        return false;
    }

    QcItemStack &source = stacks[from];
    QcItemStack &target = stacks[to];
    if (source.isEmpty() || !content.isValidItem(source.item)) {
        return true;
    }

    const int wanted = count == 0 ? source.count : std::min(count, source.count);
    if (target.isEmpty() || target.stacksWith(source)) {
        const int space = content.item(source.item).maxStack - (target.isEmpty() ? 0 : target.count);
        const int moved = std::min(wanted, space);
        if (moved <= 0) {
            return true;
        }
        target = {source.item, (target.isEmpty() ? 0 : target.count) + moved, source.damage};
        source.count -= moved;
        if (source.count <= 0) {
            source = {};
        }
        return true;
    }

    // Different items: only whole stacks swap.
    if (count == 0) {
        std::swap(source, target);
    }
    return true;
}

int QcSlotList::moveInto(int from, const QList<int> &targets, const QcContentRegistry &content)
{
    if (from < 0 || from >= stacks.size() || stacks[from].isEmpty() || !content.isValidItem(stacks[from].item)) {
        return 0;
    }

    const int before = stacks[from].count;
    // Stacks of the same items (QcItemStack::stacksWith) fill up first.
    for (const bool merging : {true, false}) {
        for (const int to : targets) {
            if (stacks[from].isEmpty()) {
                break;
            }
            if (to == from || to < 0 || to >= stacks.size()) {
                continue;
            }
            const QcItemStack &target = stacks[to];
            const bool equal = !target.isEmpty() && target.stacksWith(stacks[from]);
            if (merging ? equal : target.isEmpty()) {
                // Both indices are inside the list (checked above and by the caller).
                [[maybe_unused]] const bool moved = moveBetween(from, to, 0, content);
            }
        }
    }
    return before - (stacks[from].isEmpty() ? 0 : stacks[from].count);
}
