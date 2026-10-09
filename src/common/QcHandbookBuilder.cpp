#include "QcHandbookBuilder.h"

#include "QcMining.h"
#include "QcPhysics.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Fact = QcHandbookEntry::Fact;
using Recipe = QcHandbookEntry::Recipe;
using Section = QcHandbookEntry::Section;

namespace {

constexpr auto english = "en";

QString namespaceOf(const QString &name)
{
    const qsizetype colon = name.indexOf(u':');
    return colon < 0 ? QString() : name.left(colon);
}

QString localPart(const QString &name)
{
    return name.mid(name.indexOf(u':') + 1);
}

// "oak_tree" -> "Oak tree": the last resort for content without a translated name.
QString humanized(const QString &name)
{
    QString text = localPart(name).replace(u'_', u' ').replace(u'/', u' ');
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    return text;
}

void sortLinks(QList<Link> &links)
{
    std::ranges::stable_sort(links, [](const Link &a, const Link &b) {
        const QString keyA = QcHandbook::searchKey(a.text);
        const QString keyB = QcHandbook::searchKey(b.text);
        return keyA != keyB ? keyA < keyB : a.target < b.target;
    });
}

void appendUnique(QList<Link> &links, Link link)
{
    const auto same = std::ranges::find_if(links, [&link](const Link &other) { return other.target == link.target && !link.target.isEmpty(); });
    if (same == links.end()) {
        links.append(std::move(link));
    } else if (!link.note.isEmpty() && !same->note.contains(link.note)) {
        same->note = same->note.isEmpty() ? link.note : same->note + u", "_s + link.note;
    }
}

class Builder
{
public:
    Builder(const QcContentRegistry &content, const QString &language, const QMap<QString, double> &rules,
            const QMap<QString, QStringList> &keys)
        : m_content(content)
        , m_language(language)
        , m_rules(rules)
        , m_keys(keys)
        , m_locale(language)
    {
        m_locale.setNumberOptions(QLocale::OmitGroupSeparator);
        addContentValues();
    }

    QcHandbook build();

private:
    // Translation of a key in the handbook's language (English as fallback), empty without one.
    QString lookup(const QString &key, const QString &language) const
    {
        if (m_content.hasTranslation(key, language) || m_content.hasTranslation(key, QString::fromLatin1(english))) {
            return m_content.translate(key, language);
        }
        return {};
    }
    QString lookup(const QString &key) const { return lookup(key, m_language); }
    // Handbook label; the key itself shows up when the content lacks the translation.
    QString label(const QString &key) const { return m_content.translate(u"handbook."_s + key, m_language); }
    QString number(double value) const;
    QString seconds(double value) const { return label(u"unit.seconds"_s).arg(number(value)); }
    QString yesNo(bool value) const { return label(value ? u"value.yes"_s : u"value.no"_s); }
    QString percent(double share) const { return label(u"unit.percent"_s).arg(number(share * 100.0)); }

    QString packOf(const QString &name) const { return namespaceOf(name); }
    bool hasBlockEntry(const QString &name) const;
    QString itemEntryId(const QString &item) const;
    QString itemTitle(const QString &item, const QString &language) const;
    QString itemIcon(const QString &item) const { return m_content.itemId(item) ? item : QString(); }
    Link itemLink(const QString &item, const QString &note = {}) const;
    Link blockLink(const QString &block, const QString &note = {}) const { return itemLink(block, note); }
    Link tagLink(const QString &tag) const;
    QString tagTitle(const QString &tag, const QString &language) const;
    Link ingredientLink(const QString &ingredient) const
    {
        return ingredient.startsWith(u'#') ? tagLink(ingredient.mid(1)) : itemLink(ingredient);
    }
    Link mobLink(const QcMobDefinition &mob, const QString &note = {}) const
    {
        return {u"mob:"_s + mob.name, m_content.translate(mob.translationKey, m_language), {}, note};
    }
    Link biomeLink(const QcBiomeDefinition &biome, const QString &note = {}) const
    {
        return {u"biome:"_s + biome.name, m_content.translate(biome.translationKey, m_language), itemIcon(biome.surfaceBlock), note};
    }
    Link featureLink(const QcFeatureDefinition &feature, const QString &note = {}) const
    {
        return {u"feature:"_s + feature.name, featureTitle(feature, m_language), itemIcon(feature.trunkBlock), note};
    }
    Link oreLink(const QcOreDefinition &ore, const QString &note = {}) const
    {
        return {u"ore:"_s + ore.name, oreTitle(ore, m_language), itemIcon(ore.block), note};
    }
    Link tableLink(const QString &table) const
    {
        return {u"table:"_s + table, label(u"table."_s + table), {}, {}};
    }
    QString featureTitle(const QcFeatureDefinition &feature, const QString &language) const;
    QString oreTitle(const QcOreDefinition &ore, const QString &language) const;
    QString dropNote(const QcItemDrop &drop) const;
    QString toolName(const QString &tool) const;
    QString heightRange(int minY, int maxY) const { return label(u"unit.heightRange"_s).arg(minY).arg(maxY); }
    QString substituteRules(const QString &text, const QString &context);
    // Key names in the handbook's language ("handbook.key.<name>", else the name), joined by " / ".
    QString keyText(const QStringList &keys) const;
    // Content values texts may quote: "<block>.toolLevel", ".slots", ".flowDistance", ".light",
    // ".hardness", "<item>.toolLevel", ".food", ".fuelSeconds", ".durability", "<mob>.health".
    void addContentValues();
    // A description with its placeholders filled; empty without a translation.
    QString description(const QString &key, const QString &context)
    {
        const QString text = lookup(key);
        return text.isEmpty() ? text : substituteRules(text, context);
    }
    QString healthPoints(double value) const
    {
        return value == 1.0 ? label(u"unit.healthOne"_s) : label(u"unit.health"_s).arg(number(value));
    }
    QString foodPoints(int value) const { return value == 1 ? label(u"unit.foodOne"_s) : label(u"unit.food"_s).arg(value); }
    QString machineName(const QString &category) const
    {
        const QString name = lookup(u"handbook.machine."_s + category);
        return name.isEmpty() ? category : name;
    }

    Recipe craftingRecipe(const QcRecipeDefinition &recipe) const;
    Recipe machineRecipe(const QcSmeltingRecipe &recipe) const;
    void addCommonItemSections(QcHandbookEntry &entry, const QString &item) const;

    void indexContent();
    void addBlocks();
    void addItems();
    void addMobs();
    void addBiomes();
    void addWorldGeneration();
    void addTags();
    void addTables();
    void addChapters();
    void checkLinks();

    void add(QcHandbookEntry entry, const QString &englishTitle)
    {
        entry.englishTitle = englishTitle;
        m_entries.append(std::move(entry));
    }

    const QcContentRegistry &m_content;
    QString m_language;
    // The server's rule values plus values of the content ("qtcraft:chest.slots", see addContentValues()).
    QMap<QString, double> m_rules;
    // Key placeholders ("key.cameraView") with portable key names.
    QMap<QString, QStringList> m_keys;
    QLocale m_locale;
    QList<QcHandbookEntry> m_entries;
    QStringList m_problems;

    // Reverse lookups, all in content order.
    QHash<QString, QList<qsizetype>> m_recipesByResult;
    // Item -> recipes using it, with the tag it fits through (empty when named directly).
    QHash<QString, QList<std::pair<qsizetype, QString>>> m_recipesByIngredient;
    QHash<QString, QList<qsizetype>> m_smeltingByResult;
    QHash<QString, QList<qsizetype>> m_smeltingByInput;
    QHash<QString, QList<Link>> m_droppedBy;
    // Tag (without '#') -> member items in content order.
    QMap<QString, QStringList> m_tagMembers;
    QHash<QString, QList<Link>> m_foundIn;
    QHash<QString, QList<Link>> m_spawnersOn;
    QHash<QString, QList<qsizetype>> m_machinesByCategory;
    QHash<QString, QStringList> m_partsByBase;
};

QString Builder::number(double value) const
{
    const double rounded = std::round(value * 100.0) / 100.0;
    if (rounded == std::floor(rounded) && std::abs(rounded) < 1e15) {
        return m_locale.toString(qint64(rounded));
    }
    const int decimals = std::round(rounded * 10.0) / 10.0 == rounded ? 1 : 2;
    return m_locale.toString(rounded, 'f', decimals);
}

bool Builder::hasBlockEntry(const QString &name) const
{
    const std::optional<QcBlockId> id = m_content.blockId(name);
    return id && m_content.block(*id).renderLayer != QcRenderLayer::Invisible;
}

QString Builder::itemEntryId(const QString &item) const
{
    if (hasBlockEntry(item)) {
        return u"block:"_s + item;
    }
    if (m_content.itemId(item)) {
        return u"item:"_s + item;
    }
    return {};
}

QString Builder::itemTitle(const QString &item, const QString &language) const
{
    if (const std::optional<QcItemId> id = m_content.itemId(item)) {
        return m_content.translate(m_content.item(*id).translationKey, language);
    }
    if (const std::optional<QcBlockId> id = m_content.blockId(item)) {
        return m_content.translate(m_content.block(*id).translationKey, language);
    }
    return item;
}

Link Builder::itemLink(const QString &item, const QString &note) const
{
    QString icon = itemIcon(item);
    if (icon.isEmpty()) {
        // Liquids show their bucket.
        if (const std::optional<QcBlockId> block = m_content.blockId(item)) {
            icon = itemIcon(m_content.block(*block).flow.bucket);
        }
    }
    return {itemEntryId(item), itemTitle(item, m_language), icon, note};
}

QString Builder::tagTitle(const QString &tag, const QString &language) const
{
    const QString title = lookup(u"tag.%1.%2"_s.arg(namespaceOf(tag), localPart(tag)), language);
    return title.isEmpty() ? u"#"_s + localPart(tag) : title;
}

Link Builder::tagLink(const QString &tag) const
{
    const QStringList members = m_tagMembers.value(tag);
    return {u"tag:"_s + tag, tagTitle(tag, m_language), members.isEmpty() ? QString() : itemIcon(members.first()), {}};
}

QString Builder::featureTitle(const QcFeatureDefinition &feature, const QString &language) const
{
    const QString title = lookup(u"feature.%1.%2"_s.arg(namespaceOf(feature.name), localPart(feature.name)), language);
    return title.isEmpty() ? humanized(feature.name) : title;
}

QString Builder::oreTitle(const QcOreDefinition &ore, const QString &language) const
{
    return m_content.translate(u"handbook.ore.title"_s, language).arg(itemTitle(ore.block, language));
}

QString Builder::dropNote(const QcItemDrop &drop) const
{
    QStringList parts;
    if (drop.count != 1) {
        parts.append(label(u"unit.count"_s).arg(drop.count));
    }
    if (drop.chance < 1.0) {
        parts.append(percent(drop.chance));
    }
    return parts.join(u", "_s);
}

QString Builder::toolName(const QString &tool) const
{
    const QString name = lookup(u"handbook.tool."_s + tool);
    return name.isEmpty() ? tool : name;
}

void Builder::addContentValues()
{
    for (const QcBlockDefinition &block : m_content.blocks()) {
        m_rules.insert(block.name + u".toolLevel"_s, block.toolLevel);
        m_rules.insert(block.name + u".slots"_s, block.containerSlots);
        m_rules.insert(block.name + u".flowDistance"_s, block.flow.distance);
        m_rules.insert(block.name + u".light"_s, block.lightLevel);
        m_rules.insert(block.name + u".hardness"_s, block.hardness);
    }
    // Items of blocks share the block's name; the block's values win.
    for (const QcItemDefinition &item : m_content.items()) {
        if (m_content.blockId(item.name)) {
            continue;
        }
        m_rules.insert(item.name + u".toolLevel"_s, item.toolLevel);
        m_rules.insert(item.name + u".food"_s, item.food);
        m_rules.insert(item.name + u".fuelSeconds"_s, item.fuelSeconds);
        m_rules.insert(item.name + u".durability"_s, item.durability);
    }
    for (const QcMobDefinition &mob : m_content.mobs()) {
        m_rules.insert(mob.name + u".health"_s, mob.maxHealth);
    }
}

QString Builder::keyText(const QStringList &keys) const
{
    if (keys.isEmpty()) {
        return label(u"key.none"_s);
    }
    QStringList names;
    for (const QString &key : keys) {
        const QString name = lookup(u"handbook.key."_s + key);
        names.append(name.isEmpty() ? key : name);
    }
    return names.join(u" / "_s);
}

QString Builder::substituteRules(const QString &text, const QString &context)
{
    static const QRegularExpression placeholder(u"\\{([A-Za-z0-9_.:-]+)\\}"_s);
    // One report per text, naming at most this many distinct unknown names.
    constexpr qsizetype maxNamedUnknown = 8;
    QString result;
    QStringList unknown;
    qsizetype last = 0;
    for (QRegularExpressionMatchIterator it = placeholder.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        result += QStringView(text).mid(last, match.capturedStart() - last);
        const auto value = m_rules.constFind(match.captured(1));
        if (const auto keys = m_keys.constFind(match.captured(1)); keys != m_keys.constEnd()) {
            result += keyText(*keys);
        } else if (value == m_rules.constEnd()) {
            if (!unknown.contains(match.captured(1))) {
                unknown.append(match.captured(1));
            }
            result += match.captured(0);
        } else {
            result += number(*value);
        }
        last = match.capturedEnd();
    }
    result += QStringView(text).mid(last);
    if (unknown.size() == 1) {
        m_problems.append(u"%1: unknown rule {%2}"_s.arg(context, unknown.first()));
    } else if (!unknown.isEmpty()) {
        QStringList named;
        for (qsizetype i = 0; i < std::min(unknown.size(), maxNamedUnknown); ++i) {
            named.append(u'{' + unknown[i] + u'}');
        }
        QString problem = u"%1: unknown rules %2"_s.arg(context, named.join(u", "_s));
        if (unknown.size() > maxNamedUnknown) {
            problem += u" and %1 more"_s.arg(unknown.size() - maxNamedUnknown);
        }
        m_problems.append(problem);
    }
    return result;
}

Recipe Builder::craftingRecipe(const QcRecipeDefinition &recipe) const
{
    Recipe result;
    result.kind = Recipe::Kind::Crafting;
    result.name = recipe.name;
    result.shapeless = !recipe.shaped;
    if (recipe.shaped) {
        result.width = std::max(1, recipe.width());
        result.height = std::max(1, recipe.height());
        for (int row = 0; row < recipe.height(); ++row) {
            for (int column = 0; column < recipe.width(); ++column) {
                const QString item = recipe.itemAt(column, row);
                result.cells.append(item.isEmpty() ? Link{} : ingredientLink(item));
            }
        }
        result.needsTable = recipe.width() > 2 || recipe.height() > 2;
    } else {
        const int count = int(recipe.ingredients.size());
        result.width = count <= 1 ? 1 : (count <= 4 ? 2 : 3);
        result.height = (count + result.width - 1) / result.width;
        for (const QString &ingredient : recipe.ingredients) {
            result.cells.append(ingredientLink(ingredient));
        }
        while (result.cells.size() < qsizetype(result.width) * result.height) {
            result.cells.append(Link{});
        }
        result.needsTable = count > 4;
    }
    result.result = itemLink(recipe.resultItem);
    result.resultCount = recipe.resultCount;
    result.note = label(result.needsTable ? (result.shapeless ? u"recipe.tableShapeless"_s : u"recipe.table"_s)
                                          : (result.shapeless ? u"recipe.handShapeless"_s : u"recipe.hand"_s));
    return result;
}

Recipe Builder::machineRecipe(const QcSmeltingRecipe &recipe) const
{
    Recipe result;
    result.kind = Recipe::Kind::Machine;
    result.name = recipe.name;
    result.cells = {itemLink(recipe.inputItem)};
    result.result = itemLink(recipe.resultItem);
    result.resultCount = recipe.resultCount;
    result.seconds = recipe.seconds;
    QStringList machineNames;
    const auto listed1 = m_machinesByCategory.value(recipe.category);
    for (const qsizetype index : listed1) {
        const QcBlockDefinition &machine = m_content.blocks()[index];
        Link link = blockLink(machine.name);
        if (machine.machine.speed != 1.0) {
            link.note = label(u"unit.speed"_s).arg(number(machine.machine.speed));
        }
        machineNames.append(link.text);
        result.machines.append(std::move(link));
    }
    if (machineNames.isEmpty()) {
        machineNames.append(machineName(recipe.category));
    }
    result.note = label(u"recipe.machine"_s).arg(machineNames.join(u", "_s), seconds(recipe.seconds));
    return result;
}

void Builder::indexContent()
{
    const QList<QcRecipeDefinition> &recipes = m_content.recipes();
    for (const QcItemDefinition &item : m_content.items()) {
        for (const QString &tag : item.tags) {
            m_tagMembers[tag].append(item.name);
        }
    }
    for (qsizetype i = 0; i < recipes.size(); ++i) {
        const QcRecipeDefinition &recipe = recipes[i];
        m_recipesByResult[recipe.resultItem].append(i);
        QStringList ingredients = recipe.ingredients;
        for (const auto &[key, item] : recipe.keys) {
            ingredients.append(item);
        }
        QSet<QString> seen;
        for (const QString &ingredient : std::as_const(ingredients)) {
            if (seen.contains(ingredient)) {
                continue;
            }
            seen.insert(ingredient);
            if (ingredient.startsWith(u'#')) {
                const QString tag = ingredient.mid(1);
                if (!m_tagMembers.contains(tag)) {
                    m_problems.append(u"recipe %1: no item has the tag %2"_s.arg(recipe.name, tag));
                }
                const auto listed2 = m_tagMembers.value(tag);
                for (const QString &member : listed2) {
                    m_recipesByIngredient[member].append(std::pair<qsizetype, QString>(i, tag));
                }
            } else {
                m_recipesByIngredient[ingredient].append(std::pair<qsizetype, QString>(i, QString()));
            }
        }
    }
    const QList<QcSmeltingRecipe> &smelting = m_content.smeltingRecipes();
    for (qsizetype i = 0; i < smelting.size(); ++i) {
        m_smeltingByResult[smelting[i].resultItem].append(i);
        m_smeltingByInput[smelting[i].inputItem].append(i);
    }
    const QList<QcBlockDefinition> &blocks = m_content.blocks();
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const QcBlockDefinition &block = blocks[i];
        if (block.renderLayer == QcRenderLayer::Invisible) {
            continue;
        }
        if (block.machine.isMachine()) {
            m_machinesByCategory[block.machine.category].append(i);
        }
        for (const QcItemDrop &drop : block.drops) {
            if (drop.item != block.name) {
                appendUnique(m_droppedBy[drop.item], blockLink(block.name, dropNote(drop)));
            }
        }
        if (!block.partOf.isEmpty()) {
            const QString material = block.partOf.section(u'/', 0, 0);
            const auto it = std::ranges::find(m_content.materials(), material, &QcMaterialDefinition::name);
            if (it != m_content.materials().end()) {
                m_partsByBase[it->block].append(block.name);
            }
        }
    }
    for (const QcMobDefinition &mob : m_content.mobs()) {
        for (const QcItemDrop &drop : mob.drops) {
            appendUnique(m_droppedBy[drop.item], mobLink(mob, dropNote(drop)));
        }
        for (const QString &block : mob.spawnBlocks) {
            appendUnique(m_spawnersOn[block], mobLink(mob));
        }
        for (const QcMobDefinition::SpawnRule &rule : mob.extraSpawns) {
            for (const QString &block : rule.blocks) {
                appendUnique(m_spawnersOn[block], mobLink(mob));
            }
        }
    }
    // Where blocks occur: biome layers, features listed by biomes, ore veins and pools.
    for (const QcBiomeDefinition &biome : m_content.biomes()) {
        const std::array<std::pair<QString, QString>, 7> layers{{{biome.surfaceBlock, u"biome.surface"_s},
                                                                 {biome.subsurfaceBlock, u"biome.subsurface"_s},
                                                                 {biome.underwaterBlock, u"biome.underwater"_s},
                                                                 {biome.beachBlock, u"biome.beach"_s},
                                                                 {biome.steepBlock, u"biome.steep"_s},
                                                                 {biome.frozenWaterBlock, u"biome.frozenWater"_s},
                                                                 {biome.snowBlock, u"biome.snow"_s}}};
        for (const auto &[block, role] : layers) {
            if (!block.isEmpty()) {
                appendUnique(m_foundIn[block], biomeLink(biome, label(u"fact."_s + role)));
            }
        }
        for (const QcFeaturePlacement &placement : biome.features) {
            const auto feature = std::ranges::find(m_content.features(), placement.feature, &QcFeatureDefinition::name);
            if (feature == m_content.features().end()) {
                continue;
            }
            for (const QString &block : {feature->trunkBlock, feature->leavesBlock}) {
                if (!block.isEmpty()) {
                    appendUnique(m_foundIn[block], biomeLink(biome, featureTitle(*feature, m_language)));
                }
            }
        }
    }
    for (const QcOreDefinition &ore : m_content.ores()) {
        appendUnique(m_foundIn[ore.block], oreLink(ore, heightRange(ore.minY, ore.maxY)));
    }
    for (const QcFeatureDefinition &feature : m_content.features()) {
        if (feature.type == QcFeatureDefinition::Type::Pool) {
            appendUnique(m_foundIn[feature.trunkBlock], featureLink(feature, heightRange(feature.minY, feature.maxY)));
        }
    }
}

void Builder::addCommonItemSections(QcHandbookEntry &entry, const QString &item) const
{
    const QList<QcRecipeDefinition> &recipes = m_content.recipes();
    const QList<QcSmeltingRecipe> &smelting = m_content.smeltingRecipes();
    Section made{label(u"section.crafting"_s), {}, {}, {}};
    const auto listed3 = m_recipesByResult.value(item);
    for (const qsizetype index : listed3) {
        made.recipes.append(craftingRecipe(recipes[index]));
    }
    if (!made.recipes.isEmpty()) {
        entry.sections.append(std::move(made));
    }
    Section smelted{label(u"section.smelting"_s), {}, {}, {}};
    const auto listed4 = m_smeltingByResult.value(item);
    for (const qsizetype index : listed4) {
        smelted.recipes.append(machineRecipe(smelting[index]));
    }
    if (!smelted.recipes.isEmpty()) {
        smelted.links.append(tableLink(u"fuels"_s));
        entry.sections.append(std::move(smelted));
    }
    Section smeltsInto{label(u"section.smeltsInto"_s), {}, {}, {}};
    const auto listed5 = m_smeltingByInput.value(item);
    for (const qsizetype index : listed5) {
        smeltsInto.recipes.append(machineRecipe(smelting[index]));
    }
    if (!smeltsInto.recipes.isEmpty()) {
        smeltsInto.links.append(tableLink(u"fuels"_s));
        entry.sections.append(std::move(smeltsInto));
    }
    Section usedIn{label(u"section.usedIn"_s), {}, {}, {}};
    const auto listed6 = m_recipesByIngredient.value(item);
    for (const auto &[index, tag] : listed6) {
        appendUnique(usedIn.links, itemLink(recipes[index].resultItem, tag.isEmpty() ? QString() : tagTitle(tag, m_language)));
    }
    sortLinks(usedIn.links);
    if (!usedIn.links.isEmpty()) {
        entry.sections.append(std::move(usedIn));
    }
    QList<Link> droppedBy = m_droppedBy.value(item);
    sortLinks(droppedBy);
    if (!droppedBy.isEmpty()) {
        entry.sections.append({label(u"section.droppedBy"_s), {}, droppedBy, {}});
    }
    QList<Link> caughtFrom;
    QList<Link> catches;
    for (const QcMobDefinition &mob : m_content.mobs()) {
        if (mob.catchItem == item) {
            caughtFrom.append(mobLink(mob, itemTitle(mob.catchWith, m_language)));
        }
        if (mob.catchWith == item) {
            catches.append(mobLink(mob, itemTitle(mob.catchItem, m_language)));
        }
    }
    if (!caughtFrom.isEmpty()) {
        entry.sections.append({label(u"section.caughtFrom"_s), {}, caughtFrom, {}});
    }
    if (!catches.isEmpty()) {
        entry.sections.append({label(u"section.catches"_s), {}, catches, {}});
    }
}

void Builder::addBlocks()
{
    for (const QcBlockDefinition &block : m_content.blocks()) {
        if (block.renderLayer == QcRenderLayer::Invisible) {
            continue;
        }
        QcHandbookEntry entry;
        entry.id = u"block:"_s + block.name;
        entry.name = block.name;
        entry.category = block.liquid ? u"liquids"_s : (block.partOf.isEmpty() ? u"blocks"_s : u"parts"_s);
        entry.pack = packOf(block.name);
        entry.title = m_content.translate(block.translationKey, m_language);
        entry.icon = itemLink(block.name).icon;
        entry.text = description(block.descriptionKey, block.name);
        QList<Fact> &facts = entry.facts;
        facts.append({label(u"fact.id"_s), block.name, {}});
        // Sections that follow the recipes.
        QList<Section> later;

        if (block.liquid) {
            const QcLiquidDefinition &flow = block.flow;
            if (flow.flows()) {
                facts.append({label(u"fact.flowDistance"_s), label(u"unit.blocks"_s).arg(flow.distance), {}});
                facts.append({label(u"fact.flowStep"_s), seconds(flow.ticks * QcPhysics::tickDuration), {}});
                facts.append({label(u"fact.infinite"_s), yesNo(flow.infinite), {}});
            } else {
                facts.append({label(u"fact.flowDistance"_s), label(u"value.stays"_s), {}});
            }
            if (flow.movementFactor != 1.0) {
                facts.append({label(u"fact.movement"_s), percent(flow.movementFactor), {}});
            }
            if (!flow.bucket.isEmpty()) {
                facts.append({label(u"fact.bucket"_s), {}, {itemLink(flow.bucket)}});
            }
            for (const QcLiquidReaction &reaction : flow.reactions) {
                Fact fact{label(u"fact.reaction"_s).arg(itemTitle(reaction.with, m_language)), {}, {}};
                if (!reaction.source.isEmpty()) {
                    fact.links.append(blockLink(reaction.source, label(u"note.source"_s)));
                }
                if (!reaction.flowing.isEmpty()) {
                    fact.links.append(blockLink(reaction.flowing, label(u"note.flowing"_s)));
                }
                facts.append(std::move(fact));
            }
        } else if (!QcMining::isBreakable(block)) {
            facts.append({label(u"fact.hardness"_s), label(u"value.unbreakable"_s), {}});
        } else {
            facts.append({label(u"fact.hardness"_s), number(block.hardness), {}});
            facts.append({label(u"fact.timeByHand"_s), seconds(QcMining::breakSeconds(block, nullptr)), {}});
            if (!block.tool.isEmpty()) {
                const QString level = block.toolLevel > 0 ? label(u"value.toolRequired"_s).arg(toolName(block.tool)).arg(block.toolLevel)
                                                          : label(u"value.toolFaster"_s).arg(toolName(block.tool));
                facts.append({label(u"fact.tool"_s), level, {}});
                const QcItemDefinition *best = nullptr;
                QList<std::pair<int, Link>> tools;
                for (const QcItemDefinition &item : m_content.items()) {
                    if (item.tool != block.tool || !QcMining::canHarvest(block, &item)) {
                        continue;
                    }
                    tools.append({item.toolLevel, itemLink(item.name, label(u"unit.level"_s).arg(item.toolLevel))});
                    if (!best || QcMining::breakSeconds(block, &item) < QcMining::breakSeconds(block, best)) {
                        best = &item;
                    }
                }
                if (best) {
                    facts.append({label(u"fact.timeBestTool"_s), seconds(QcMining::breakSeconds(block, best)), {itemLink(best->name)}});
                }
                // Weakest fitting tool first.
                std::ranges::stable_sort(tools, [](const auto &a, const auto &b) {
                    return a.first != b.first ? a.first < b.first : QcHandbook::searchKey(a.second.text) < QcHandbook::searchKey(b.second.text);
                });
                Section toolSection{label(block.toolLevel > 0 ? u"section.minedWith"_s : u"section.fasterWith"_s), {}, {}, {}};
                for (auto &[level, link] : tools) {
                    toolSection.links.append(std::move(link));
                }
                if (!toolSection.links.isEmpty()) {
                    later.append(std::move(toolSection));
                }
            }
            Fact drops{label(u"fact.drops"_s), {}, {}};
            for (const QcItemDrop &drop : block.drops) {
                drops.links.append(itemLink(drop.item, dropNote(drop)));
            }
            const bool needsTool = !drops.links.isEmpty() && block.toolLevel > 0;
            if (drops.links.isEmpty()) {
                drops.value = label(u"value.nothing"_s);
            }
            facts.append(std::move(drops));
            if (needsTool) {
                facts.append({label(u"fact.withoutTool"_s), label(u"value.nothing"_s), {}});
            }
        }
        if (block.lightLevel > 0) {
            facts.append({label(u"fact.light"_s), label(u"unit.lightLevel"_s).arg(block.lightLevel), {}});
        }
        if (block.lightOpacity < 15) {
            facts.append({label(u"fact.lightOpacity"_s), label(u"unit.lightLevel"_s).arg(block.lightOpacity), {}});
        }
        if (!block.solid) {
            facts.append({label(u"fact.solid"_s), yesNo(false), {}});
        }
        if (block.replaceable) {
            facts.append({label(u"fact.replaceable"_s), yesNo(true), {}});
        }
        if (block.support != QcBlockSupport::None || !block.groundBlocks.isEmpty()) {
            Fact support{label(u"fact.support"_s), {}, {}};
            if (!block.groundBlocks.isEmpty()) {
                support.value = label(u"value.supportGround"_s);
                for (const QString &ground : block.groundBlocks) {
                    support.links.append(blockLink(ground));
                }
            } else {
                support.value = label(block.support == QcBlockSupport::Below ? u"value.supportBelow"_s : u"value.supportAttached"_s);
            }
            facts.append(std::move(support));
        }
        if (!block.groundLiquid.isEmpty()) {
            facts.append({label(u"fact.groundLiquid"_s), {}, {blockLink(block.groundLiquid)}});
        }
        if (block.falls) {
            facts.append({label(u"fact.falls"_s), yesNo(true), {}});
        }
        if (block.washable) {
            facts.append({label(u"fact.washable"_s), yesNo(true), {}});
        }
        if (block.contactDamage > 0.0) {
            facts.append({label(u"fact.contactDamage"_s), healthPoints(block.contactDamage), {}});
        }
        if (block.climbable) {
            facts.append({label(u"fact.climbable"_s), yesNo(true), {}});
        }
        if (block.toggles) {
            facts.append({label(u"fact.toggles"_s), yesNo(true), {}});
        }
        if (block.door) {
            facts.append({label(u"fact.door"_s), yesNo(true), {}});
        }
        if (block.opensCrafting) {
            facts.append({label(u"fact.crafting"_s), label(u"value.craftingGrid"_s), {}});
        }
        if (block.containerSlots > 0) {
            facts.append({label(u"fact.storage"_s), label(u"unit.slots"_s).arg(block.containerSlots), {}});
        }
        if (block.machine.isMachine()) {
            facts.append({label(u"fact.machine"_s), label(u"value.machine"_s).arg(machineName(block.machine.category), number(block.machine.speed)), {tableLink(u"fuels"_s)}});
            Section processes{label(u"section.processes"_s), {}, {}, {}};
            for (const QcSmeltingRecipe &recipe : m_content.smeltingRecipes()) {
                if (recipe.category == block.machine.category) {
                    processes.recipes.append(machineRecipe(recipe));
                }
            }
            if (!processes.recipes.isEmpty()) {
                later.append(std::move(processes));
            }
        }
        if (block.fuelSeconds > 0.0) {
            facts.append({label(u"fact.fuel"_s), seconds(block.fuelSeconds), {tableLink(u"fuels"_s)}});
        }
        if (block.sapling.isSapling()) {
            const auto feature = std::ranges::find(m_content.features(), block.sapling.feature, &QcFeatureDefinition::name);
            Fact grows{label(u"fact.growsInto"_s),
                       label(u"unit.secondsRange"_s).arg(number(block.sapling.minSeconds), number(block.sapling.maxSeconds)),
                       {}};
            if (feature != m_content.features().end()) {
                grows.links.append(featureLink(*feature));
            }
            facts.append(std::move(grows));
            if (!block.sapling.plantOn.isEmpty()) {
                Fact plantOn{label(u"fact.plantOn"_s), {}, {}};
                for (const QString &ground : block.sapling.plantOn) {
                    plantOn.links.append(blockLink(ground));
                }
                facts.append(std::move(plantOn));
            }
            if (block.sapling.plantChance > 0.0) {
                facts.append({label(u"fact.plantChance"_s), percent(block.sapling.plantChance), {}});
            }
        }
        if (!block.partOf.isEmpty()) {
            const QString material = block.partOf.section(u'/', 0, 0);
            const QString family = block.partOf.section(u'/', 1);
            const auto base = std::ranges::find(m_content.materials(), material, &QcMaterialDefinition::name);
            if (base != m_content.materials().end()) {
                facts.append({label(u"fact.madeFrom"_s), {}, {blockLink(base->block)}});
            }
            const QString familyTitle = lookup(u"handbook.family."_s + localPart(family));
            facts.append({label(u"fact.family"_s), familyTitle.isEmpty() ? humanized(family) : familyTitle, {}});
        }
        if (!block.connectGroup.isEmpty() || !block.connectsTo.isEmpty()) {
            QStringList groups;
            for (const QString &group : block.connectsTo) {
                const QString title = lookup(u"handbook.group."_s + localPart(group));
                groups.append(title.isEmpty() ? humanized(group) : title);
            }
            groups.append(label(u"value.fullBlocks"_s));
            facts.append({label(u"fact.connects"_s), groups.join(u", "_s), {}});
        }
        if (!block.soundMaterial.isEmpty()) {
            const QString sound = lookup(u"handbook.sound."_s + localPart(block.soundMaterial));
            facts.append({label(u"fact.sound"_s), sound.isEmpty() ? humanized(block.soundMaterial) : sound, {}});
        }
        if (!block.properties.isEmpty()) {
            QStringList states;
            for (const QcBlockProperty &property : block.properties) {
                states.append(u"%1 (%2)"_s.arg(property.name, property.values.join(u", "_s)));
            }
            facts.append({label(u"fact.states"_s), states.join(u"; "_s), {}});
        }
        if (!block.itemTags.isEmpty()) {
            Fact tags{label(u"fact.tags"_s), {}, {}};
            for (const QString &tag : block.itemTags) {
                tags.links.append(tagLink(tag));
            }
            facts.append(std::move(tags));
        }

        if (const auto it = m_partsByBase.constFind(block.name); it != m_partsByBase.constEnd()) {
            Section parts{label(u"section.parts"_s), {}, {}, {}};
            for (const QString &part : *it) {
                parts.links.append(blockLink(part));
            }
            sortLinks(parts.links);
            entry.sections.append(std::move(parts));
        }
        addCommonItemSections(entry, block.name);
        entry.sections += later;
        QList<Link> found = m_foundIn.value(block.name);
        sortLinks(found);
        if (!found.isEmpty()) {
            entry.sections.append({label(u"section.foundIn"_s), {}, found, {}});
        }
        QList<Link> spawners = m_spawnersOn.value(block.name);
        sortLinks(spawners);
        if (!spawners.isEmpty()) {
            entry.sections.append({label(u"section.spawnsOn"_s), {}, spawners, {}});
        }
        add(std::move(entry), m_content.translate(block.translationKey, QString::fromLatin1(english)));
    }
}

void Builder::addItems()
{
    for (const QcItemDefinition &item : m_content.items()) {
        if (hasBlockEntry(item.name)) {
            continue;
        }
        QcHandbookEntry entry;
        entry.id = u"item:"_s + item.name;
        entry.name = item.name;
        const bool tool = !item.tool.isEmpty() || item.attackDamage > 1.0;
        entry.category = tool ? u"tools"_s : (item.food > 0 ? u"food"_s : u"items"_s);
        entry.pack = packOf(item.name);
        entry.title = m_content.translate(item.translationKey, m_language);
        entry.icon = item.name;
        entry.text = description(item.descriptionKey, item.name);
        QList<Fact> &facts = entry.facts;
        facts.append({label(u"fact.id"_s), item.name, {}});
        facts.append({label(u"fact.maxStack"_s), QString::number(item.maxStack), {}});
        if (!item.tool.isEmpty()) {
            facts.append({label(u"fact.tool"_s), label(u"value.toolTier"_s).arg(toolName(item.tool)).arg(item.toolLevel), {tableLink(u"tools"_s)}});
            facts.append({label(u"fact.toolSpeed"_s), label(u"unit.factor"_s).arg(number(item.toolSpeed)), {}});
        }
        if (item.durability > 0) {
            facts.append({label(u"fact.durability"_s), label(u"unit.uses"_s).arg(item.durability), {}});
        }
        if (tool) {
            facts.append({label(u"fact.attackDamage"_s), healthPoints(item.attackDamage), {}});
        }
        if (item.food > 0) {
            facts.append({label(u"fact.food"_s), foodPoints(item.food), {}});
        }
        if (item.fuelSeconds > 0.0) {
            facts.append({label(u"fact.fuel"_s), seconds(item.fuelSeconds), {tableLink(u"fuels"_s)}});
        }
        if (!item.placesBlock.isEmpty()) {
            facts.append({label(u"fact.places"_s), {}, {blockLink(item.placesBlock)}});
        }
        if (!item.emptiesTo.isEmpty()) {
            facts.append({label(u"fact.emptiesTo"_s), {}, {itemLink(item.emptiesTo)}});
        }
        if (item.scoops) {
            Fact scoops{label(u"fact.scoops"_s), {}, {}};
            for (const QcBlockDefinition &block : m_content.blocks()) {
                if (block.liquid && !block.flow.bucket.isEmpty()) {
                    scoops.links.append(blockLink(block.name, itemTitle(block.flow.bucket, m_language)));
                }
            }
            facts.append(std::move(scoops));
        }
        if (!item.tags.isEmpty()) {
            Fact tags{label(u"fact.tags"_s), {}, {}};
            for (const QString &tag : item.tags) {
                tags.links.append(tagLink(tag));
            }
            facts.append(std::move(tags));
        }
        if (!item.tool.isEmpty()) {
            Section harvests{label(u"section.harvests"_s), {}, {}, {}};
            for (const QcBlockDefinition &block : m_content.blocks()) {
                if (block.renderLayer != QcRenderLayer::Invisible && block.tool == item.tool && block.toolLevel > 0
                    && QcMining::canHarvest(block, &item)) {
                    harvests.links.append(blockLink(block.name, label(u"unit.level"_s).arg(block.toolLevel)));
                }
            }
            sortLinks(harvests.links);
            addCommonItemSections(entry, item.name);
            if (!harvests.links.isEmpty()) {
                entry.sections.append(std::move(harvests));
            }
        } else {
            addCommonItemSections(entry, item.name);
        }
        add(std::move(entry), m_content.translate(item.translationKey, QString::fromLatin1(english)));
    }
}

void Builder::addMobs()
{
    for (const QcMobDefinition &mob : m_content.mobs()) {
        QcHandbookEntry entry;
        entry.id = u"mob:"_s + mob.name;
        entry.name = mob.name;
        entry.category = u"creatures"_s;
        entry.pack = packOf(mob.name);
        entry.title = m_content.translate(mob.translationKey, m_language);
        entry.icon = mob.drops.isEmpty() ? itemIcon(mob.catchItem) : itemIcon(mob.drops.first().item);
        entry.text = description(mob.descriptionKey, mob.name);
        QList<Fact> &facts = entry.facts;
        facts.append({label(u"fact.id"_s), mob.name, {}});
        const QString kind = mob.ambient ? u"ambient"_s : (mob.hostile ? u"hostile"_s : u"passive"_s);
        facts.append({label(u"fact.behaviour"_s), label(u"value.mob."_s + kind), {}});
        facts.append({label(u"fact.health"_s), healthPoints(mob.maxHealth), {}});
        facts.append({label(u"fact.size"_s), label(u"unit.size"_s).arg(number(mob.width), number(mob.height)), {}});
        // Hostile creatures run at chaseSpeed after players, the others when they flee.
        const QString speeds = mob.chaseSpeed == mob.walkSpeed ? label(u"unit.metresPerSecond"_s).arg(number(mob.walkSpeed))
                               : label(mob.hostile ? u"unit.speeds"_s : u"unit.speedsFleeing"_s).arg(number(mob.walkSpeed), number(mob.chaseSpeed));
        facts.append({label(u"fact.speed"_s), speeds, {}});
        if (mob.hostile) {
            facts.append({label(u"fact.attack"_s),
                          label(u"value.attack"_s).arg(number(mob.attackDamage), number(mob.attackRange), number(mob.attackCooldown)),
                          {}});
            facts.append({label(u"fact.sight"_s), label(u"unit.metres"_s).arg(number(mob.sightRange)), {}});
        }
        const QString time = mob.spawnTime == QcMobDefinition::SpawnTime::Day     ? u"day"_s
                             : mob.spawnTime == QcMobDefinition::SpawnTime::Night ? u"night"_s
                                                                                  : u"any"_s;
        facts.append({label(u"fact.spawnTime"_s), label(u"value.time."_s + time), {}});
        if (mob.spawnsInCaves) {
            facts.append({label(u"fact.caves"_s), label(u"value.caves"_s), {}});
        }
        if (mob.maxSpawnLight < QcBlockDefinition::maxLightLevel) {
            facts.append({label(u"fact.spawnLight"_s), label(u"value.maxLight"_s).arg(mob.maxSpawnLight), {}});
        }
        if (mob.spawnMaxY < QcWorld::worldHeight) {
            facts.append({label(u"fact.spawnMaxY"_s), label(u"value.belowY"_s).arg(mob.spawnMaxY), {}});
        }
        if (mob.spawnChunkChance < 1.0) {
            facts.append({label(u"fact.spawnChunks"_s), label(u"value.spawnChunks"_s).arg(number(100.0 * mob.spawnChunkChance)), {}});
        }
        if (!mob.hostile && !mob.ambient) {
            facts.append({label(u"fact.persistent"_s), label(u"value.persistent"_s), {}});
        }
        facts.append({label(u"fact.group"_s), label(u"value.group"_s).arg(mob.maxGroup), {}});
        facts.append({label(u"fact.weight"_s), number(mob.spawnWeight), {}});
        const QString capRule = u"mobs.%1Cap"_s.arg(kind);
        if (const auto cap = m_rules.constFind(capRule); cap != m_rules.constEnd()) {
            const QString categoryCap = label(u"value.categoryCap."_s + kind).arg(number(*cap));
            facts.append({label(u"fact.cap"_s),
                          mob.maxCount > 0 ? label(u"value.cap"_s).arg(mob.maxCount) + u"; "_s + categoryCap : categoryCap, {}});
        } else if (mob.maxCount > 0) {
            facts.append({label(u"fact.cap"_s), label(u"value.cap"_s).arg(mob.maxCount), {}});
        }
        if (mob.flying) {
            facts.append({label(u"fact.flying"_s), yesNo(true), {}});
        }
        if (mob.glow > 0) {
            facts.append({label(u"fact.glow"_s), label(u"unit.lightLevel"_s).arg(mob.glow), {}});
        }
        if (!mob.catchWith.isEmpty()) {
            facts.append({label(u"fact.catch"_s), {}, {itemLink(mob.catchWith), itemLink(mob.catchItem)}});
        }
        Fact drops{label(u"fact.drops"_s), {}, {}};
        for (const QcItemDrop &drop : mob.drops) {
            drops.links.append(itemLink(drop.item, dropNote(drop)));
        }
        if (drops.links.isEmpty()) {
            drops.value = label(u"value.nothing"_s);
        }
        facts.append(std::move(drops));

        Section spawnsOn{label(u"section.spawnBlocks"_s), {}, {}, {}};
        for (const QString &block : mob.spawnBlocks) {
            spawnsOn.links.append(blockLink(block));
        }
        if (spawnsOn.links.isEmpty()) {
            spawnsOn.text = label(mob.flying ? u"value.anySolidOrLiquid"_s : u"value.anySolid"_s);
        }
        sortLinks(spawnsOn.links);
        entry.sections.append(std::move(spawnsOn));
        Section biomes{label(u"section.spawnBiomes"_s), {}, {}, {}};
        for (const QcBiomeDefinition &biome : m_content.biomes()) {
            if (mob.spawnBiomes.contains(biome.name)) {
                biomes.links.append(biomeLink(biome));
            }
        }
        sortLinks(biomes.links);
        if (mob.spawnBiomes.isEmpty()) {
            biomes.text = label(u"value.allBiomes"_s);
        }
        entry.sections.append(std::move(biomes));
        // Further places and times (slimes on swamp surfaces at night): conditions as text, biomes
        // and blocks as links.
        for (const QcMobDefinition::SpawnRule &rule : mob.extraSpawns) {
            const QString ruleTime = rule.time == QcMobDefinition::SpawnTime::Day     ? u"day"_s
                                     : rule.time == QcMobDefinition::SpawnTime::Night ? u"night"_s
                                                                                      : u"any"_s;
            QStringList conditions{label(u"value.time."_s + ruleTime)};
            if (rule.caves) {
                conditions.append(label(u"value.caves"_s));
            }
            if (rule.maxY < QcWorld::worldHeight) {
                conditions.append(label(u"value.belowY"_s).arg(rule.maxY));
            }
            if (rule.maxLight < QcBlockDefinition::maxLightLevel) {
                conditions.append(label(u"value.maxLight"_s).arg(rule.maxLight));
            }
            if (rule.chunkChance < 1.0) {
                conditions.append(label(u"value.spawnChunks"_s).arg(number(100.0 * rule.chunkChance)));
            }
            Section also{label(u"section.alsoSpawns"_s), conditions.join(u"; "_s), {}, {}};
            for (const QcBiomeDefinition &biome : m_content.biomes()) {
                if (rule.biomes.contains(biome.name)) {
                    also.links.append(biomeLink(biome));
                }
            }
            for (const QString &block : rule.blocks) {
                also.links.append(blockLink(block));
            }
            if (rule.biomes.isEmpty()) {
                also.text += u"; "_s + label(u"value.allBiomes"_s);
            }
            entry.sections.append(std::move(also));
        }
        add(std::move(entry), m_content.translate(mob.translationKey, QString::fromLatin1(english)));
    }
}

void Builder::addBiomes()
{
    for (const QcBiomeDefinition &biome : m_content.biomes()) {
        QcHandbookEntry entry;
        entry.id = u"biome:"_s + biome.name;
        entry.name = biome.name;
        entry.category = u"biomes"_s;
        entry.pack = packOf(biome.name);
        entry.title = m_content.translate(biome.translationKey, m_language);
        entry.icon = itemIcon(biome.surfaceBlock);
        entry.text = description(biome.descriptionKey, biome.name);
        QList<Fact> &facts = entry.facts;
        facts.append({label(u"fact.id"_s), biome.name, {}});
        facts.append({label(u"fact.climate"_s), label(u"value.climate"_s).arg(number(biome.temperature), number(biome.humidity), number(biome.ruggedness)), {}});
        facts.append({label(u"fact.height"_s), label(u"value.height"_s).arg(number(biome.baseHeight), number(biome.heightVariation)), {}});
        const auto layer = [&](const QString &block, const QString &key, const QString &note = {}) {
            if (!block.isEmpty()) {
                facts.append({label(u"fact."_s + key), {}, {blockLink(block, note)}});
            }
        };
        layer(biome.surfaceBlock, u"biome.surface"_s);
        layer(biome.subsurfaceBlock, u"biome.subsurface"_s, label(u"unit.blocks"_s).arg(biome.subsurfaceDepth));
        layer(biome.underwaterBlock, u"biome.underwater"_s);
        layer(biome.beachBlock, u"biome.beach"_s);
        layer(biome.steepBlock, u"biome.steep"_s, label(u"value.steep"_s).arg(biome.steepSlope));
        layer(biome.frozenWaterBlock, u"biome.frozenWater"_s);
        if (biome.snowLine > 0) {
            layer(biome.snowBlock, u"biome.snow"_s, label(u"value.above"_s).arg(biome.snowLine));
        }
        Section features{label(u"section.features"_s), {}, {}, {}};
        for (const QcFeaturePlacement &placement : biome.features) {
            const auto feature = std::ranges::find(m_content.features(), placement.feature, &QcFeatureDefinition::name);
            if (feature == m_content.features().end()) {
                m_problems.append(u"biome %1: unknown feature %2"_s.arg(biome.name, placement.feature));
                continue;
            }
            features.links.append(featureLink(*feature, label(u"unit.perColumn"_s).arg(number(placement.density * 100.0))));
        }
        sortLinks(features.links);
        if (!features.links.isEmpty()) {
            entry.sections.append(std::move(features));
        }
        Section creatures{label(u"section.creatures"_s), {}, {}, {}};
        for (const QcMobDefinition &mob : m_content.mobs()) {
            const bool extra = std::ranges::any_of(mob.extraSpawns, [&](const QcMobDefinition::SpawnRule &rule) {
                return rule.biomes.isEmpty() || rule.biomes.contains(biome.name);
            });
            if (mob.spawnBiomes.isEmpty() || mob.spawnBiomes.contains(biome.name) || extra) {
                creatures.links.append(mobLink(mob));
            }
        }
        sortLinks(creatures.links);
        if (!creatures.links.isEmpty()) {
            entry.sections.append(std::move(creatures));
        }
        add(std::move(entry), m_content.translate(biome.translationKey, QString::fromLatin1(english)));
    }
}

void Builder::addWorldGeneration()
{
    const QString basePack = m_content.packs().isEmpty() ? QString() : m_content.packs().first().id;
    const QcTerrainSettings &terrain = m_content.terrainSettings();
    if (!terrain.stoneBlock.isEmpty()) {
        QcHandbookEntry entry;
        entry.id = u"world:terrain"_s;
        entry.category = u"worldgen"_s;
        entry.pack = basePack;
        entry.title = label(u"world.terrain"_s);
        entry.icon = itemIcon(terrain.stoneBlock);
        entry.text = label(u"world.terrainText"_s);
        entry.facts.append({label(u"fact.seaLevel"_s), label(u"value.y"_s).arg(terrain.seaLevel), {}});
        entry.facts.append({label(u"fact.stone"_s), {}, {blockLink(terrain.stoneBlock)}});
        entry.facts.append({label(u"fact.water"_s), {}, {blockLink(terrain.waterBlock)}});
        if (!terrain.bedrockBlock.isEmpty()) {
            entry.facts.append({label(u"fact.bedrock"_s), {}, {blockLink(terrain.bedrockBlock)}});
        }
        entry.facts.append({label(u"fact.caves"_s), yesNo(terrain.caves), {}});
        Section ores{label(u"section.ores"_s), {}, {}, {}};
        for (const QcOreDefinition &ore : m_content.ores()) {
            ores.links.append(oreLink(ore, heightRange(ore.minY, ore.maxY)));
        }
        sortLinks(ores.links);
        entry.sections.append(std::move(ores));
        Section biomes{label(u"section.biomes"_s), {}, {}, {}};
        for (const QcBiomeDefinition &biome : m_content.biomes()) {
            biomes.links.append(biomeLink(biome));
        }
        sortLinks(biomes.links);
        entry.sections.append(std::move(biomes));
        add(std::move(entry), m_content.translate(u"handbook.world.terrain"_s, QString::fromLatin1(english)));
    }
    for (const QcOreDefinition &ore : m_content.ores()) {
        QcHandbookEntry entry;
        entry.id = u"ore:"_s + ore.name;
        entry.name = ore.name;
        entry.category = u"worldgen"_s;
        entry.pack = packOf(ore.name);
        entry.title = oreTitle(ore, m_language);
        entry.icon = itemIcon(ore.block);
        entry.facts.append({label(u"fact.id"_s), ore.name, {}});
        entry.facts.append({label(u"fact.block"_s), {}, {blockLink(ore.block)}});
        const QString replaces = ore.replaces.isEmpty() ? terrain.stoneBlock : ore.replaces;
        if (!replaces.isEmpty()) {
            entry.facts.append({label(u"fact.replaces"_s), {}, {blockLink(replaces)}});
        }
        entry.facts.append({label(u"fact.heights"_s), heightRange(ore.minY, ore.maxY), {}});
        entry.facts.append({label(u"fact.veins"_s), label(u"value.veins"_s).arg(ore.veinsPerChunk).arg(ore.veinSize), {}});
        add(std::move(entry), oreTitle(ore, QString::fromLatin1(english)));
    }
    for (const QcFeatureDefinition &feature : m_content.features()) {
        QcHandbookEntry entry;
        entry.id = u"feature:"_s + feature.name;
        entry.name = feature.name;
        entry.category = u"worldgen"_s;
        entry.pack = packOf(feature.name);
        entry.title = featureTitle(feature, m_language);
        entry.icon = itemIcon(feature.trunkBlock);
        entry.facts.append({label(u"fact.id"_s), feature.name, {}});
        static constexpr std::array<const char16_t *, 4> types{u"tree", u"column", u"plant", u"pool"};
        const QString kind = feature.type == QcFeatureDefinition::Type::Pool && feature.surface
                                 ? u"surfacePool"_s
                                 : QString::fromUtf16(types[std::size_t(feature.type)]);
        entry.facts.append({label(u"fact.featureType"_s), label(u"value.feature."_s + kind), {}});
        Fact blocks{label(u"fact.blocks"_s), {}, {}};
        for (const QString &block : {feature.trunkBlock, feature.leavesBlock}) {
            if (!block.isEmpty()) {
                blocks.links.append(blockLink(block));
            }
        }
        entry.facts.append(std::move(blocks));
        if (feature.type == QcFeatureDefinition::Type::Pool) {
            if (!feature.surface) {
                entry.facts.append({label(u"fact.heights"_s), heightRange(feature.minY, feature.maxY), {}});
            }
            entry.facts.append({label(u"fact.poolChance"_s),
                                label(u"value.pool"_s).arg(feature.attempts).arg(percent(feature.chance)), {}});
            entry.facts.append({label(u"fact.poolSize"_s), label(u"value.poolSize"_s).arg(feature.radius).arg(feature.depth), {}});
        } else {
            entry.facts.append({label(u"fact.featureHeight"_s), label(u"unit.blockRange"_s).arg(feature.minHeight).arg(feature.maxHeight), {}});
        }
        if (!feature.groundBlocks.isEmpty()) {
            Fact ground{label(u"fact.ground"_s), {}, {}};
            for (const QString &block : feature.groundBlocks) {
                ground.links.append(blockLink(block));
            }
            entry.facts.append(std::move(ground));
        }
        if (feature.nearWater) {
            entry.facts.append({label(u"fact.nearWater"_s), yesNo(true), {}});
        }
        Section biomes{label(u"section.inBiomes"_s), {}, {}, {}};
        for (const QcBiomeDefinition &biome : m_content.biomes()) {
            for (const QcFeaturePlacement &placement : biome.features) {
                if (placement.feature == feature.name) {
                    appendUnique(biomes.links, biomeLink(biome, label(u"unit.perColumn"_s).arg(number(placement.density * 100.0))));
                }
            }
        }
        sortLinks(biomes.links);
        if (feature.type == QcFeatureDefinition::Type::Pool) {
            for (const QcBiomeDefinition &biome : m_content.biomes()) {
                if (feature.biomes.contains(biome.name)) {
                    appendUnique(biomes.links, biomeLink(biome));
                }
            }
            sortLinks(biomes.links);
            biomes.text = feature.biomes.isEmpty() ? label(u"value.everyChunk"_s) : label(u"value.poolBiomes"_s);
        }
        for (const QcBlockDefinition &block : m_content.blocks()) {
            if (block.sapling.feature == feature.name) {
                entry.facts.append({label(u"fact.grownFrom"_s), {}, {blockLink(block.name)}});
            }
        }
        if (!biomes.links.isEmpty() || !biomes.text.isEmpty()) {
            entry.sections.append(std::move(biomes));
        }
        add(std::move(entry), featureTitle(feature, QString::fromLatin1(english)));
    }
}

void Builder::addTags()
{
    const QList<QcRecipeDefinition> &recipes = m_content.recipes();
    for (auto it = m_tagMembers.constBegin(); it != m_tagMembers.constEnd(); ++it) {
        const QString &tag = it.key();
        QcHandbookEntry entry;
        entry.id = u"tag:"_s + tag;
        entry.name = tag;
        entry.category = u"tags"_s;
        entry.pack = packOf(tag);
        entry.title = tagTitle(tag, m_language);
        entry.icon = itemIcon(it->first());
        entry.text = label(u"tag.text"_s);
        entry.facts.append({label(u"fact.id"_s), u"#"_s + tag, {}});
        Section members{label(u"section.members"_s), {}, {}, {}};
        for (const QString &member : *it) {
            members.links.append(itemLink(member));
        }
        sortLinks(members.links);
        entry.sections.append(std::move(members));
        Section usedIn{label(u"section.usedIn"_s), {}, {}, {}};
        for (const QcRecipeDefinition &recipe : recipes) {
            if (recipe.ingredients.contains(u"#"_s + tag)
                || std::ranges::any_of(recipe.keys, [&tag](const auto &key) { return key.second == u"#"_s + tag; })) {
                appendUnique(usedIn.links, itemLink(recipe.resultItem));
            }
        }
        sortLinks(usedIn.links);
        if (!usedIn.links.isEmpty()) {
            entry.sections.append(std::move(usedIn));
        }
        add(std::move(entry), tagTitle(tag, QString::fromLatin1(english)));
    }
}

void Builder::addTables()
{
    const QString basePack = m_content.packs().isEmpty() ? QString() : m_content.packs().first().id;
    const auto table = [&](const QString &name) {
        QcHandbookEntry entry;
        entry.id = u"table:"_s + name;
        entry.category = u"tables"_s;
        entry.pack = basePack;
        entry.title = label(u"table."_s + name);
        entry.text = label(u"table."_s + name + u"Text"_s);
        return entry;
    };

    QcHandbookEntry fuels = table(u"fuels"_s);
    fuels.icon = itemIcon(u"qtcraft:coal"_s);
    QList<std::pair<double, Link>> burning;
    for (const QcItemDefinition &item : m_content.items()) {
        if (item.fuelSeconds > 0.0) {
            burning.append({item.fuelSeconds, itemLink(item.name, seconds(item.fuelSeconds))});
        }
    }
    std::ranges::stable_sort(burning, [](const auto &a, const auto &b) {
        if (a.first != b.first) {
            return a.first > b.first;
        }
        return QcHandbook::searchKey(a.second.text) < QcHandbook::searchKey(b.second.text);
    });
    Section fuelList{label(u"section.fuels"_s), {}, {}, {}};
    for (auto &[secondsBurning, link] : burning) {
        fuelList.links.append(std::move(link));
    }
    fuels.sections.append(std::move(fuelList));
    Section machines{label(u"section.machines"_s), {}, {}, {}};
    for (const QcBlockDefinition &block : m_content.blocks()) {
        if (block.machine.isMachine()) {
            machines.links.append(blockLink(block.name));
        }
    }
    sortLinks(machines.links);
    fuels.sections.append(std::move(machines));
    add(std::move(fuels), m_content.translate(u"handbook.table.fuels"_s, QString::fromLatin1(english)));

    QcHandbookEntry tools = table(u"tools"_s);
    tools.icon = itemIcon(u"qtcraft:iron_pickaxe"_s);
    QStringList categories;
    for (const QcItemDefinition &item : m_content.items()) {
        if (!item.tool.isEmpty() && !categories.contains(item.tool)) {
            categories.append(item.tool);
        }
    }
    for (const QString &category : std::as_const(categories)) {
        QList<std::pair<int, Link>> tiers;
        for (const QcItemDefinition &item : m_content.items()) {
            if (item.tool == category) {
                tiers.append({item.toolLevel, itemLink(item.name, label(u"value.tier"_s).arg(item.toolLevel).arg(number(item.toolSpeed)).arg(item.durability))});
            }
        }
        std::ranges::stable_sort(tiers, [](const auto &a, const auto &b) {
            return a.first != b.first ? a.first < b.first : QcHandbook::searchKey(a.second.text) < QcHandbook::searchKey(b.second.text);
        });
        Section section{toolName(category), {}, {}, {}};
        for (auto &[level, link] : tiers) {
            section.links.append(std::move(link));
        }
        QMap<int, QStringList> needs;
        for (const QcBlockDefinition &block : m_content.blocks()) {
            if (block.tool == category && block.toolLevel > 0 && block.partOf.isEmpty()) {
                needs[block.toolLevel].append(m_content.translate(block.translationKey, m_language));
            }
        }
        QStringList lines;
        for (auto it = needs.begin(); it != needs.end(); ++it) {
            std::ranges::sort(*it, [](const QString &a, const QString &b) { return QcHandbook::searchKey(a) < QcHandbook::searchKey(b); });
            lines.append(u"- "_s + label(u"value.levelNeeds"_s).arg(it.key()).arg(it->join(u", "_s)));
        }
        section.text = lines.join(u'\n');
        tools.sections.append(std::move(section));
    }
    add(std::move(tools), m_content.translate(u"handbook.table.tools"_s, QString::fromLatin1(english)));

    QcHandbookEntry smelting = table(u"smelting"_s);
    smelting.icon = itemIcon(u"qtcraft:furnace"_s);
    Section all{label(u"section.machineRecipes"_s), {}, {}, {}};
    for (const QcSmeltingRecipe &recipe : m_content.smeltingRecipes()) {
        all.recipes.append(machineRecipe(recipe));
    }
    std::ranges::stable_sort(all.recipes, [](const Recipe &a, const Recipe &b) {
        const QString keyA = QcHandbook::searchKey(a.result.text);
        const QString keyB = QcHandbook::searchKey(b.result.text);
        return keyA != keyB ? keyA < keyB : a.name < b.name;
    });
    all.links.append(tableLink(u"fuels"_s));
    smelting.sections.append(std::move(all));
    add(std::move(smelting), m_content.translate(u"handbook.table.smelting"_s, QString::fromLatin1(english)));
}

void Builder::addChapters()
{
    for (const QcHandbookChapter &chapter : m_content.handbookChapters()) {
        QcHandbookEntry entry;
        entry.id = u"chapter:"_s + chapter.name;
        entry.name = chapter.name;
        entry.category = u"playing"_s;
        entry.pack = packOf(chapter.name);
        entry.title = m_content.translate(chapter.titleKey, m_language);
        entry.icon = itemIcon(chapter.iconItem);
        for (const QString &name : chapter.sections) {
            const QString key = chapter.titleKey + u'.' + name;
            const QString text = lookup(key + u".text"_s);
            if (text.isEmpty()) {
                m_problems.append(u"chapter %1: section %2 has no text"_s.arg(chapter.name, name));
                continue;
            }
            entry.sections.append({lookup(key + u".title"_s), substituteRules(text, u"chapter "_s + chapter.name), {}, {}});
        }
        Section related{label(u"section.related"_s), {}, {}, {}};
        for (const QString &name : chapter.related) {
            Link link = itemLink(name);
            if (link.target.isEmpty()) {
                if (const std::optional<int> mob = m_content.mobIndex(name)) {
                    link = mobLink(m_content.mobs()[*mob]);
                } else if (const auto biome = std::ranges::find(m_content.biomes(), name, &QcBiomeDefinition::name);
                           biome != m_content.biomes().end()) {
                    link = biomeLink(*biome);
                }
            }
            related.links.append(std::move(link));
        }
        if (!related.links.isEmpty()) {
            entry.sections.append(std::move(related));
        }
        add(std::move(entry), m_content.translate(chapter.titleKey, QString::fromLatin1(english)));
    }
}

void Builder::checkLinks()
{
    QSet<QString> ids;
    for (const QcHandbookEntry &entry : std::as_const(m_entries)) {
        if (ids.contains(entry.id)) {
            m_problems.append(u"duplicate entry %1"_s.arg(entry.id));
        }
        ids.insert(entry.id);
    }
    const auto check = [&](const QcHandbookEntry &entry, const Link &link) {
        if (link.isEmpty()) {
            return;
        }
        if (link.target.isEmpty()) {
            m_problems.append(u"%1: '%2' has no entry"_s.arg(entry.id, link.text));
        } else if (!ids.contains(link.target)) {
            m_problems.append(u"%1: link to unknown entry %2"_s.arg(entry.id, link.target));
        }
    };
    for (const QcHandbookEntry &entry : std::as_const(m_entries)) {
        for (const Fact &fact : entry.facts) {
            for (const Link &link : fact.links) {
                check(entry, link);
            }
        }
        for (const Section &section : entry.sections) {
            for (const Link &link : section.links) {
                check(entry, link);
            }
            for (const Recipe &recipe : section.recipes) {
                for (const Link &cell : recipe.cells) {
                    check(entry, cell);
                }
                check(entry, recipe.result);
                for (const Link &machine : recipe.machines) {
                    check(entry, machine);
                }
            }
        }
    }
}

QcHandbook Builder::build()
{
    indexContent();
    addChapters();
    addBlocks();
    addItems();
    addMobs();
    addBiomes();
    addWorldGeneration();
    addTags();
    addTables();
    checkLinks();

    QList<QcHandbook::Pack> packs;
    QHash<QString, qsizetype> packOrder;
    for (const QcPackReference &pack : m_content.packs()) {
        if (!packOrder.contains(pack.id)) {
            packOrder.insert(pack.id, packs.size());
            packs.append({pack.id, pack.name, pack.version});
        }
    }
    QStringList otherNamespaces;
    for (const QcHandbookEntry &entry : std::as_const(m_entries)) {
        if (!packOrder.contains(entry.pack) && !otherNamespaces.contains(entry.pack)) {
            otherNamespaces.append(entry.pack);
        }
    }
    std::ranges::sort(otherNamespaces);
    for (const QString &name : std::as_const(otherNamespaces)) {
        packOrder.insert(name, packs.size());
        packs.append({name, name, {}});
    }

    static constexpr std::array<const char16_t *, 12> categoryIds{u"playing", u"blocks", u"parts", u"liquids", u"items", u"tools",
                                                                  u"food", u"creatures", u"biomes", u"worldgen", u"tags", u"tables"};
    QList<QcHandbook::Category> categories;
    QHash<QString, qsizetype> categoryIndex;
    for (const char16_t *id : categoryIds) {
        const QString name = QString::fromUtf16(id);
        categoryIndex.insert(name, categories.size());
        categories.append({name, label(u"category."_s + name), {}});
    }
    QList<QString> sortKeys;
    sortKeys.reserve(m_entries.size());
    QHash<QString, int> chapterOrder;
    for (const QcHandbookChapter &chapter : m_content.handbookChapters()) {
        chapterOrder.insert(u"chapter:"_s + chapter.name, chapter.order);
    }
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const QcHandbookEntry &entry = m_entries[i];
        categories[categoryIndex.value(entry.category)].entries.append(i);
        sortKeys.append(QcHandbook::searchKey(entry.title));
    }
    for (QcHandbook::Category &category : categories) {
        const bool fixedOrder = category.id == u"tables"_s;
        std::ranges::stable_sort(category.entries, [&](qsizetype a, qsizetype b) {
            const QcHandbookEntry &first = m_entries[a];
            const QcHandbookEntry &second = m_entries[b];
            const qsizetype packA = packOrder.value(first.pack);
            const qsizetype packB = packOrder.value(second.pack);
            if (packA != packB) {
                return packA < packB;
            }
            if (fixedOrder) {
                return false;
            }
            const int orderA = chapterOrder.value(first.id);
            const int orderB = chapterOrder.value(second.id);
            if (orderA != orderB) {
                return orderA < orderB;
            }
            return sortKeys[a] != sortKeys[b] ? sortKeys[a] < sortKeys[b] : first.id < second.id;
        });
    }
    QMap<QString, QString> labels;
    for (const QString &key : {u"title"_s, u"intro"_s, u"contents"_s, u"index"_s, u"packs"_s, u"entries"_s, u"fact.id"_s}) {
        labels.insert(key, key == u"intro"_s ? substituteRules(label(key), u"handbook intro"_s) : label(key));
    }
    return QcHandbook(m_language, std::move(packs), std::move(categories), std::move(m_entries), std::move(m_problems),
                      std::move(labels));
}

} // namespace

namespace QcHandbookBuilder {

QcHandbook build(const QcContentRegistry &content, const QString &language, const QMap<QString, double> &rules,
                 const QMap<QString, QStringList> &keys)
{
    return Builder(content, language, rules, keys).build();
}

} // namespace QcHandbookBuilder
