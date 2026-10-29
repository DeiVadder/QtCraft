#include "common/content/registry/QcAdvancementCatalog.h"

#include "common/content/registry/QcContentRegistry.h"

#include <QHash>
#include <QSet>

using namespace Qt::StringLiterals;

namespace {

bool hasItemOrTag(const QcContentRegistry &content, const QString &name)
{
    if (!name.startsWith(u'#')) {
        return content.itemId(name).has_value();
    }
    const QString tag = name.mid(1);
    return std::ranges::any_of(content.items(), [&tag](const QcItemDefinition &item) { return item.tags.contains(tag); });
}

// What the conditions of `criterion` name that the content does not have.
QStringList unknownNames(const QcContentRegistry &content, const QcAdvancementCriterion &criterion)
{
    const QcAdvancementConditions &c = criterion.conditions;
    QStringList unknown;
    if (!c.item.isEmpty() && !hasItemOrTag(content, c.item)) {
        unknown.append(c.item);
    }
    if (!c.block.isEmpty() && !hasItemOrTag(content, c.block) && !content.blockId(c.block)) {
        unknown.append(c.block);
    }
    if (!c.entity.isEmpty() && !content.mobIndex(c.entity)) {
        unknown.append(c.entity);
    }
    if (!c.biome.isEmpty() && !content.worldgen().biome(c.biome)) {
        unknown.append(c.biome);
    }
    if (!c.effect.isEmpty() && !content.effect(c.effect)) {
        unknown.append(c.effect);
    }
    return unknown;
}

bool handbookTargetExists(const QcContentRegistry &content, const QString &link)
{
    const qsizetype colon = link.indexOf(u':');
    const QString kind = link.left(colon);
    const QString name = link.mid(colon + 1);
    if (kind == u"chapter"_s) {
        return std::ranges::any_of(content.handbookChapters(), [&name](const QcHandbookChapter &chapter) { return chapter.name == name; });
    }
    if (kind == u"item"_s) {
        return content.itemId(name).has_value();
    }
    if (kind == u"block"_s) {
        return content.blockId(name).has_value();
    }
    if (kind == u"mob"_s) {
        return content.mobIndex(name).has_value();
    }
    return content.worldgen().biome(name) != nullptr;
}

// The problems of one advancement with the rest of the content.
QStringList checkAgainst(const QcContentRegistry &content, const QcAdvancementDefinition &advancement)
{
    QStringList warnings;
    const QString where = u"advancement '%1'"_s.arg(advancement.name);
    if (!content.itemId(advancement.icon)) {
        warnings.append(u"%1: icon names no item '%2'"_s.arg(where, advancement.icon));
    }
    if (!advancement.handbook.isEmpty() && !handbookTargetExists(content, advancement.handbook)) {
        warnings.append(u"%1: the handbook has no entry '%2'"_s.arg(where, advancement.handbook));
    }
    for (const QString &key : {advancement.titleKey, advancement.descriptionKey}) {
        if (!content.translations().has(key, u"en"_s)) {
            warnings.append(u"%1: no English text for '%2'"_s.arg(where, key));
        }
    }
    for (const QcAdvancementCriterion &criterion : advancement.criteria) {
        const QStringList unknown = unknownNames(content, criterion);
        for (const QString &name : unknown) {
            warnings.append(u"%1: criterion '%2' names '%3', which the content does not have; it never matches"_s.arg(where, criterion.name, name));
        }
    }
    return warnings;
}

} // namespace

void QcAdvancementCatalog::removeDisabled()
{
    QHash<QString, QString> parentOf;
    QStringList disabled;
    for (const QcAdvancementDefinition &advancement : m_advancements.items()) {
        parentOf.insert(advancement.name, advancement.parent);
        if (advancement.disabled) {
            disabled.append(advancement.name);
        }
    }

    // The nearest ancestor that stays; a bounded walk, loops are cut afterwards.
    const auto keptAncestor = [&](QString parent) {
        for (qsizetype step = 0; step <= maxAdvancements && disabled.contains(parent); ++step) {
            parent = parentOf.value(parent);
        }
        return disabled.contains(parent) ? QString() : parent;
    };
    m_advancements.updateAll([&](QcAdvancementDefinition &advancement) { advancement.parent = keptAncestor(advancement.parent); });
    for (const QString &name : std::as_const(disabled)) {
        m_advancements.remove(name);
    }
}

QStringList QcAdvancementCatalog::cutLoops()
{
    QStringList warnings;
    m_advancements.updateAll([&](QcAdvancementDefinition &advancement) {
        if (advancement.parent.isEmpty()) {
            return;
        }
        if (!m_advancements.find(advancement.parent)) {
            warnings.append(u"advancement '%1': no parent '%2'; it becomes a root"_s.arg(advancement.name, advancement.parent));
            advancement.parent.clear();
            return;
        }

        QSet<QString> seen{advancement.name};
        for (QString parent = advancement.parent; !parent.isEmpty();) {
            if (seen.contains(parent)) {
                warnings.append(u"advancement '%1': its parents form a loop; it becomes a root"_s.arg(advancement.name));
                advancement.parent.clear();
                return;
            }
            seen.insert(parent);
            const QcAdvancementDefinition *next = m_advancements.find(parent);
            parent = next ? next->parent : QString();
        }
    });
    return warnings;
}

QStringList QcAdvancementCatalog::resolve(const QcContentRegistry &content)
{
    removeDisabled();
    QStringList warnings;
    for (qsizetype index = maxAdvancements; index < m_advancements.items().size(); ++index) {
        warnings.append(u"advancement '%1': more than %2 advancements; left out"_s.arg(m_advancements.items()[index].name).arg(maxAdvancements));
    }
    m_advancements.truncate(maxAdvancements);
    warnings.append(cutLoops());
    for (const QcAdvancementDefinition &advancement : m_advancements.items()) {
        warnings.append(checkAgainst(content, advancement));
    }
    return warnings;
}
