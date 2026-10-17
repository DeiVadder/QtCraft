#include "worldgen/showcase/QcShowcaseCategories.h"

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

QString renderName(QcRenderLayer layer)
{
    switch (layer) {
    case QcRenderLayer::Opaque:
        return u"opaque"_s;
    case QcRenderLayer::Cutout:
        return u"cutout"_s;
    case QcRenderLayer::Translucent:
        return u"translucent"_s;
    case QcRenderLayer::Invisible:
        break;
    }
    return u"invisible"_s;
}

bool isPlant(const QcBlockDefinition &block)
{
    return block.support == QcBlockSupport::Below || !block.groundBlocks.isEmpty() || block.sapling.isSapling()
           || block.shapeName == u"cross"_s;
}

bool isFunctional(const QcBlockDefinition &block)
{
    return block.opensCrafting || block.machine.isMachine() || block.containerSlots > 0 || block.toggles || block.door;
}

// An unset condition holds; a set one must equal the block's property.
bool holds(std::optional<bool> condition, bool value)
{
    return !condition || *condition == value;
}

bool anyHolds(const QStringList &choices, const std::function<bool(const QString &)> &test)
{
    return choices.isEmpty() || std::ranges::any_of(choices, test);
}

} // namespace

namespace QcShowcaseCategories {

bool matches(const QcShowcaseMatch &match, const QcBlockDefinition &block)
{
    const bool flags = holds(match.liquid, block.liquid) && holds(match.part, !block.partOf.isEmpty())
                       && holds(match.light, block.lightLevel > 0) && holds(match.plant, isPlant(block))
                       && holds(match.functional, isFunctional(block)) && holds(match.falls, block.falls)
                       && holds(match.signal, block.signal.isCircuitPart());
    if (!flags) {
        return false;
    }

    const QString path = block.name.mid(block.name.indexOf(u':') + 1);
    return anyHolds(match.render, [&block](const QString &layer) { return layer == renderName(block.renderLayer); })
           && anyHolds(match.tools, [&block](const QString &tool) { return tool == block.tool; })
           && anyHolds(match.nameContains, [&path](const QString &part) { return path.contains(part); });
}

int categoryOf(const QcBlockDefinition &block, const QcContentRegistry &content)
{
    const QList<QcShowcaseCategory> &categories = content.showcase().categories;
    for (int i = 0; i < categories.size(); ++i) {
        if (matches(categories[i].match, block)) {
            return i;
        }
    }
    return std::max(0, int(categories.size()) - 1);
}

QList<QList<quint32>> sort(const QcContentRegistry &content)
{
    QList<QList<quint32>> sorted(std::max<qsizetype>(1, content.showcase().categories.size()));
    for (qsizetype type = 0; type < content.blocks().size(); ++type) {
        const QcBlockDefinition &block = content.blocks()[type];
        if (block.renderLayer == QcRenderLayer::Invisible || block.name == QLatin1StringView(QcContentRegistry::airName)) {
            continue;
        }
        sorted[categoryOf(block, content)].append(quint32(type));
    }
    return sorted;
}

} // namespace QcShowcaseCategories
