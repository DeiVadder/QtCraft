#include "common/gameplay/QcRepair.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <cmath>

namespace QcRepair {

[[nodiscard]] std::optional<QcItemStack> result(const QcContentRegistry &content, const QList<QcItemStack> &grid)
{
    QList<QcItemStack> filled;
    for (const QcItemStack &cell : grid) {
        if (!cell.isEmpty()) {
            filled.append(cell);
        }
    }

    if (filled.size() != 2 || filled[0].item != filled[1].item || !content.isValidItem(filled[0].item)) {
        return std::nullopt;
    }

    const int durability = content.item(filled[0].item).durability;
    if (durability <= 0 || filled[0].count != 1 || filled[1].count != 1 || (filled[0].damage <= 0 && filled[1].damage <= 0)) {
        return std::nullopt;
    }

    const int bonus = int(std::floor(durability * bonusShare));
    const int remaining = (durability - filled[0].damage) + (durability - filled[1].damage) + bonus;
    return QcItemStack{filled[0].item, 1, std::max(0, durability - remaining)};
}

} // namespace QcRepair
