#include "worldgen/showcase/QcShowcaseArmourArea.h"

#include <QHash>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcShowcaseArmourArea {

QStringList sets(const QcContentRegistry &content)
{
    QHash<QString, int> points;
    QStringList order;
    for (const QcItemDefinition &item : content.items()) {
        if (!item.armour) {
            continue;
        }
        if (!points.contains(item.armour->set)) {
            order.append(item.armour->set);
        }
        points[item.armour->set] += item.armour->points;
    }

    std::ranges::stable_sort(order, [&points](const QString &a, const QString &b) { return std::tie(points[a], a) < std::tie(points[b], b); });
    return order;
}

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor)
{
    const QcContentRegistry &content = context.content;
    const QStringList armourSets = sets(content);

    // A stand on a podium every two metres, facing south where people come from.
    constexpr int depth = 4;
    const int count = std::max(1, int(armourSets.size()));
    auto structure = std::make_shared<QcStructure>(u"showcase/armour"_s, 2 * count + 1, 5, depth);
    for (qsizetype i = 0; i < armourSets.size(); ++i) {
        const int x = 1 + 2 * int(i);
        constexpr int z = 1;
        structure->set(x, 1, z, context.podium);

        const QString &set = armourSets[i];
        structure->stands.append({{x + 0.5, 2.0, z + 0.5}, 180.0f, {u"equip "_s + set}});
        const QString key = u"armour_set."_s + QString(set).replace(u':', u'.');
        structure->labels.append({{x + 0.5, 4.6, z + 0.5}, key, {}, false});
    }

    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - depth + 1};
    return {u"armour"_s, u"showcase.area.armour"_s, std::move(structure), origin, {}, {}};
}

} // namespace QcShowcaseArmourArea
