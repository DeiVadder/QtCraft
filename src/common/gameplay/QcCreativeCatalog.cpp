#include "common/gameplay/QcCreativeCatalog.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/handbook/QcHandbook.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcCreativeCatalog::QcCreativeCatalog(const QcContentRegistry &content, const QString &language)
{
    const QList<QcItemDefinition> &items = content.items();
    m_entries.reserve(items.size());
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QcItemDefinition &item = items[i];
        // A filled map from the catalogue would carry no map id (decision E-798): maps come from
        // empty ones.
        if (item.map == QcMapItem::Filled) {
            continue;
        }
        const QString label = content.translations().translate(item.translationKey, language);
        const QString english = content.translations().translate(item.translationKey, u"en"_s);
        // "qtcraft:oak_planks" is also found as "oak planks".
        const QString id = item.name.section(u':', -1).replace(u'_', u' ');
        const QString search = QcHandbook::searchKey(label + u'\n' + english + u'\n' + item.name + u'\n' + id);
        m_entries.append({static_cast<QcItemId>(i + 1), categoryOf(content, item), label, search});
    }
    // "All" lists the tabs one after the other.
    std::ranges::stable_sort(m_entries, {}, &Entry::category);
}

QcCreativeCatalog::Category QcCreativeCatalog::categoryOf(const QcContentRegistry &content, const QcItemDefinition &item)
{
    if (!item.placesBlock.isEmpty() && item.placesBlock == item.name) {
        const std::optional<QcBlockId> block = content.blockId(item.placesBlock);
        if (block && !content.block(*block).liquid) {
            if (content.block(*block).signal.isCircuitPart()) {
                return Category::Signal;
            }
            return content.block(*block).partOf.isEmpty() ? Category::Blocks : Category::Parts;
        }
    }
    if (!item.tool.isEmpty() || item.armour || item.ignites || item.launcher) {
        return Category::Tools;
    }
    return item.food > 0 ? Category::Food : Category::Items;
}

QList<const QcCreativeCatalog::Entry *> QcCreativeCatalog::filter(Category category, const QString &query) const
{
    const QStringList words = QcHandbook::searchKey(query).simplified().split(u' ', Qt::SkipEmptyParts);
    QList<const Entry *> found;
    for (const Entry &entry : m_entries) {
        if (category != Category::All && entry.category != category) {
            continue;
        }
        const bool matches = std::ranges::all_of(words, [&entry](const QString &word) { return entry.searchText.contains(word); });
        if (matches) {
            found.append(&entry);
        }
    }
    return found;
}
