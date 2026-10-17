#include "worldgen/showcase/QcShowcaseItemArea.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcShowcaseItemArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor)
{
    const QcContentRegistry &content = context.content;
    const QcBlockDefinition &chestBlock = content.block(context.container);
    const int perChest = std::max(1, chestBlock.containerSlots);
    const qsizetype items = content.items().size();
    const int chests = std::max(1, int((items + perChest - 1) / perChest));

    // Chests on a podium every two metres, their fronts to the south where people come from.
    constexpr int depth = 4;
    const QcBlockId chest = content.withProperty(context.container, u"facing"_s, u"south"_s);
    auto structure = std::make_shared<QcStructure>(u"showcase/items"_s, 2 * chests + 1, 5, depth);
    for (int i = 0; i < chests; ++i) {
        const int x = 1 + 2 * i;
        constexpr int z = 1;
        structure->set(x, 1, z, context.podium);
        structure->set(x, 2, z, chest);

        QcStructure::Contents contents{{x, 2, z}, {}};
        const qsizetype first = qsizetype(i) * perChest;
        const qsizetype last = std::min(items, first + perChest);
        for (qsizetype item = first; item < last; ++item) {
            contents.items.append({content.items()[item].name, 1});
        }
        structure->contents.append(contents);
        structure->labels.append({{x + 0.5, 3.5, z + 0.5}, u"showcase.items"_s, u"%1–%2"_s.arg(first + 1).arg(last), false});
    }

    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - depth + 1};
    return {u"items"_s, u"showcase.area.items"_s, std::move(structure), origin, {}};
}

} // namespace QcShowcaseItemArea
