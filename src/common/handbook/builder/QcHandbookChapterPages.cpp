#include "common/handbook/builder/QcHandbookChapterPages.h"

#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Section = QcHandbookEntry::Section;

namespace {

// An item or block, else a creature, else a biome; a link without target when none has the name.
Link relatedLink(const QcHandbookContext &context, const QString &name)
{
    const QcContentRegistry &content = context.content;
    Link link = context.links.itemLink(name);
    if (!link.target.isEmpty()) {
        return link;
    }

    if (const std::optional<int> mob = content.mobIndex(name)) {
        return context.links.mobLink(content.mobs()[*mob]);
    }
    if (const auto biome = std::ranges::find(content.biomes(), name, &QcBiomeDefinition::name); biome != content.biomes().end()) {
        return context.links.biomeLink(*biome);
    }
    return link;
}

QcHandbookEntry chapterPage(const QcHandbookContext &context, const QcHandbookChapter &chapter)
{
    QcHandbookText &text = context.text;
    QcHandbookEntry entry;
    entry.id = u"chapter:"_s + chapter.name;
    entry.name = chapter.name;
    entry.category = u"playing"_s;
    entry.pack = QcHandbookLinks::packOf(chapter.name);
    entry.title = context.content.translate(chapter.titleKey, text.language());
    entry.icon = context.links.itemIcon(chapter.iconItem);

    for (const QString &name : chapter.sections) {
        const QString key = chapter.titleKey + u'.' + name;
        const QString sectionText = text.lookup(key + u".text"_s);
        if (sectionText.isEmpty()) {
            text.addProblem(u"chapter %1: section %2 has no text"_s.arg(chapter.name, name));
            continue;
        }
        entry.sections.append({text.lookup(key + u".title"_s), text.substituteRules(sectionText, u"chapter "_s + chapter.name), {}, {}});
    }

    Section related{text.label(u"section.related"_s), {}, {}, {}};
    for (const QString &name : chapter.related) {
        related.links.append(relatedLink(context, name));
    }
    if (!related.links.isEmpty()) {
        entry.sections.append(std::move(related));
    }

    entry.englishTitle = context.content.translate(chapter.titleKey, QcHandbookText::english());
    return entry;
}

} // namespace

namespace QcHandbookChapterPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context)
{
    QList<QcHandbookEntry> entries;
    for (const QcHandbookChapter &chapter : context.content.handbookChapters()) {
        entries.append(chapterPage(context, chapter));
    }
    return entries;
}

} // namespace QcHandbookChapterPages
