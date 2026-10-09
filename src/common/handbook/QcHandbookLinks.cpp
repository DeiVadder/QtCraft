#include "QcHandbookLinks.h"

#include "QcHandbook.h"
#include "QcHandbookText.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;

QcHandbookLinks::QcHandbookLinks(const QcContentRegistry &content, const QcHandbookText &text)
    : m_content(content)
    , m_text(text)
{
    for (const QcItemDefinition &item : m_content.items()) {
        for (const QString &tag : item.tags) {
            m_tagMembers[tag].append(item.name);
        }
    }
}

QString QcHandbookLinks::packOf(const QString &name)
{
    const qsizetype colon = name.indexOf(u':');
    return colon < 0 ? QString() : name.left(colon);
}

QString QcHandbookLinks::localPart(const QString &name)
{
    return name.mid(name.indexOf(u':') + 1);
}

QString QcHandbookLinks::humanized(const QString &name)
{
    QString text = localPart(name).replace(u'_', u' ').replace(u'/', u' ');
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    return text;
}

void QcHandbookLinks::sortLinks(QList<Link> &links)
{
    std::ranges::stable_sort(links, [](const Link &a, const Link &b) {
        const QString keyA = QcHandbook::searchKey(a.text);
        const QString keyB = QcHandbook::searchKey(b.text);
        return keyA != keyB ? keyA < keyB : a.target < b.target;
    });
}

void QcHandbookLinks::appendUnique(QList<Link> &links, Link link)
{
    const auto same = std::ranges::find_if(links, [&link](const Link &other) { return other.target == link.target && !link.target.isEmpty(); });
    if (same == links.end()) {
        links.append(std::move(link));
    } else if (!link.note.isEmpty() && !same->note.contains(link.note)) {
        same->note = same->note.isEmpty() ? link.note : same->note + u", "_s + link.note;
    }
}

QString QcHandbookLinks::basePack() const
{
    return m_content.packs().isEmpty() ? QString() : m_content.packs().first().id;
}

bool QcHandbookLinks::hasBlockEntry(const QString &name) const
{
    const std::optional<QcBlockId> id = m_content.blockId(name);
    return id && m_content.block(*id).renderLayer != QcRenderLayer::Invisible;
}

QString QcHandbookLinks::itemEntryId(const QString &item) const
{
    if (hasBlockEntry(item)) {
        return u"block:"_s + item;
    }
    if (m_content.itemId(item)) {
        return u"item:"_s + item;
    }
    return {};
}

QString QcHandbookLinks::itemTitle(const QString &item, const QString &language) const
{
    if (const std::optional<QcItemId> id = m_content.itemId(item)) {
        return m_content.translate(m_content.item(*id).translationKey, language);
    }
    if (const std::optional<QcBlockId> id = m_content.blockId(item)) {
        return m_content.translate(m_content.block(*id).translationKey, language);
    }
    return item;
}

QString QcHandbookLinks::tagTitle(const QString &tag, const QString &language) const
{
    const QString title = m_text.lookup(u"tag.%1.%2"_s.arg(packOf(tag), localPart(tag)), language);
    return title.isEmpty() ? u"#"_s + localPart(tag) : title;
}

QString QcHandbookLinks::featureTitle(const QcFeatureDefinition &feature, const QString &language) const
{
    const QString title = m_text.lookup(u"feature.%1.%2"_s.arg(packOf(feature.name), localPart(feature.name)), language);
    return title.isEmpty() ? humanized(feature.name) : title;
}

QString QcHandbookLinks::oreTitle(const QcOreDefinition &ore, const QString &language) const
{
    return m_content.translate(u"handbook.ore.title"_s, language).arg(itemTitle(ore.block, language));
}

Link QcHandbookLinks::itemLink(const QString &item, const QString &note) const
{
    QString icon = itemIcon(item);
    if (icon.isEmpty()) {
        // Liquids show their bucket.
        if (const std::optional<QcBlockId> block = m_content.blockId(item)) {
            icon = itemIcon(m_content.block(*block).flow.bucket);
        }
    }

    return {itemEntryId(item), itemTitle(item, m_text.language()), icon, note};
}

Link QcHandbookLinks::tagLink(const QString &tag) const
{
    const QStringList members = m_tagMembers.value(tag);
    return {u"tag:"_s + tag, tagTitle(tag, m_text.language()), members.isEmpty() ? QString() : itemIcon(members.first()), {}};
}

Link QcHandbookLinks::ingredientLink(const QString &ingredient) const
{
    return ingredient.startsWith(u'#') ? tagLink(ingredient.mid(1)) : itemLink(ingredient);
}

Link QcHandbookLinks::mobLink(const QcMobDefinition &mob, const QString &note) const
{
    return {u"mob:"_s + mob.name, m_content.translate(mob.translationKey, m_text.language()), {}, note};
}

Link QcHandbookLinks::biomeLink(const QcBiomeDefinition &biome, const QString &note) const
{
    return {u"biome:"_s + biome.name, m_content.translate(biome.translationKey, m_text.language()), itemIcon(biome.surfaceBlock), note};
}

Link QcHandbookLinks::featureLink(const QcFeatureDefinition &feature, const QString &note) const
{
    return {u"feature:"_s + feature.name, featureTitle(feature, m_text.language()), itemIcon(feature.trunkBlock), note};
}

Link QcHandbookLinks::oreLink(const QcOreDefinition &ore, const QString &note) const
{
    return {u"ore:"_s + ore.name, oreTitle(ore, m_text.language()), itemIcon(ore.block), note};
}

Link QcHandbookLinks::tableLink(const QString &table) const
{
    return {u"table:"_s + table, m_text.label(u"table."_s + table), {}, {}};
}
