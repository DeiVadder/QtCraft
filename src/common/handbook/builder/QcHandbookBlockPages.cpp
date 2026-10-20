#include "common/handbook/builder/QcHandbookBlockPages.h"

#include "common/gameplay/QcMining.h"
#include "common/handbook/builder/QcHandbookBlockTables.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookIndex.h"
#include "common/handbook/builder/QcHandbookRecipes.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbook.h"
#include "common/handbook/QcHandbookLinks.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Fact = QcHandbookEntry::Fact;
using Section = QcHandbookEntry::Section;

namespace {

// Builds the page of one block: facts in a fixed order, then the sections.
class BlockPage
{
public:
    BlockPage(const QcHandbookContext &context, const QcBlockDefinition &block)
        : m_content(context.content)
        , m_text(context.text)
        , m_links(context.links)
        , m_index(context.index)
        , m_recipes(context.recipes)
        , m_block(block)
    {
    }

    QcHandbookEntry build();

private:
    QString label(const QString &key) const { return m_text.label(key); }
    void addFact(const QString &key, const QString &value, const QList<Link> &links = {})
    {
        m_entry.facts.append({label(u"fact."_s + key), value, links});
    }
    void addFlag(bool set, const QString &key)
    {
        if (set) {
            addFact(key, m_text.yesNo(true));
        }
    }

    void addLiquidFacts();
    void addMiningFacts();
    void addToolFacts();
    void addDropFacts();
    void addPhysicalFacts();
    void addSupportFacts();
    // Light the block holds back as the light engine applies it.
    void addLightBlockingFact();
    // Slowing inside, the walking surface and hardening (cobweb, mud, concrete powder).
    void addMovementFacts();
    void addUseFacts();
    void addMachineFacts();
    void addSaplingFacts();
    // Stages, pace, light and fruit of crops, moisture of soil, tilling (decisions E-289 to E-291).
    void addFarmingFacts();
    void addSoilFacts();
    // "when ripe", "while growing" or the plain states a drop needs.
    QString dropCondition(const QcItemDrop &drop) const;
    void addPartFacts();
    void addDescriptiveFacts();
    void addSections();

    const QcContentRegistry &m_content;
    QcHandbookText &m_text;
    const QcHandbookLinks &m_links;
    const QcHandbookIndex &m_index;
    const QcHandbookRecipes &m_recipes;
    const QcBlockDefinition &m_block;
    QcHandbookEntry m_entry;
    // Sections that follow the recipes.
    QList<Section> m_later;
};

QcHandbookEntry BlockPage::build()
{
    const QcBlockDefinition &block = m_block;
    m_entry.id = u"block:"_s + block.name;
    m_entry.name = block.name;
    m_entry.category = block.liquid ? u"liquids"_s : (block.partOf.isEmpty() ? u"blocks"_s : u"parts"_s);
    m_entry.pack = QcHandbookLinks::packOf(block.name);
    m_entry.title = m_content.translations().translate(block.translationKey, m_text.language());
    m_entry.icon = m_links.itemLink(block.name).icon;
    m_entry.text = m_text.description(block.descriptionKey, block.name);

    addFact(u"id"_s, block.name);
    if (block.liquid) {
        addLiquidFacts();
    } else if (!QcMining::isBreakable(block)) {
        addFact(u"hardness"_s, label(u"value.unbreakable"_s));
    } else {
        addMiningFacts();
    }

    addPhysicalFacts();
    addUseFacts();
    addSaplingFacts();
    addFarmingFacts();
    addPartFacts();
    addDescriptiveFacts();
    addSections();

    m_entry.englishTitle = m_content.translations().translate(block.translationKey, QcHandbookText::english());
    return std::move(m_entry);
}

void BlockPage::addLiquidFacts()
{
    const QcLiquidDefinition &flow = m_block.flow;
    if (flow.flows()) {
        addFact(u"flowDistance"_s, label(u"unit.blocks"_s).arg(flow.distance));
        addFact(u"flowStep"_s, m_text.seconds(flow.ticks * QcPhysics::tickDuration));
        addFact(u"infinite"_s, m_text.yesNo(flow.infinite));
    } else {
        addFact(u"flowDistance"_s, label(u"value.stays"_s));
    }

    if (flow.movementFactor != 1.0) {
        addFact(u"movement"_s, m_text.percent(flow.movementFactor));
    }
    if (!flow.bucket.isEmpty()) {
        addFact(u"bucket"_s, {}, {m_links.itemLink(flow.bucket)});
    }

    for (const QcLiquidReaction &reaction : flow.reactions) {
        Fact fact{label(u"fact.reaction"_s).arg(m_links.itemTitle(reaction.with, m_text.language())), {}, {}};
        if (!reaction.source.isEmpty()) {
            fact.links.append(m_links.blockLink(reaction.source, label(u"note.source"_s)));
        }
        if (!reaction.flowing.isEmpty()) {
            fact.links.append(m_links.blockLink(reaction.flowing, label(u"note.flowing"_s)));
        }
        m_entry.facts.append(std::move(fact));
    }
}

void BlockPage::addMiningFacts()
{
    addFact(u"hardness"_s, m_text.number(m_block.hardness));
    addFact(u"timeByHand"_s, m_text.seconds(QcMining::breakSeconds(m_block, nullptr)));
    if (!m_block.tool.isEmpty()) {
        addToolFacts();
    }
    addDropFacts();
}

void BlockPage::addToolFacts()
{
    const QcBlockDefinition &block = m_block;
    const QString level = block.toolLevel > 0
                              ? label(u"value.toolRequired"_s).arg(m_text.toolName(block.tool)).arg(block.toolLevel)
                              : label(u"value.toolFaster"_s).arg(m_text.toolName(block.tool));
    addFact(u"tool"_s, level);

    const QcItemDefinition *best = nullptr;
    QList<std::pair<int, Link>> tools;
    for (const QcItemDefinition &item : m_content.items()) {
        if (item.tool != block.tool || !QcMining::canHarvest(block, &item)) {
            continue;
        }
        tools.append({item.toolLevel, m_links.itemLink(item.name, label(u"unit.level"_s).arg(item.toolLevel))});
        if (!best || QcMining::breakSeconds(block, &item) < QcMining::breakSeconds(block, best)) {
            best = &item;
        }
    }

    if (best) {
        addFact(u"timeBestTool"_s, m_text.seconds(QcMining::breakSeconds(block, best)), {m_links.itemLink(best->name)});
    }

    // Weakest fitting tool first.
    std::ranges::stable_sort(tools, [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first < b.first : QcHandbook::searchKey(a.second.text) < QcHandbook::searchKey(b.second.text);
    });
    Section toolSection{label(block.toolLevel > 0 ? u"section.minedWith"_s : u"section.fasterWith"_s), {}, {}, {}};
    for (auto &[toolLevel, link] : tools) {
        toolSection.links.append(std::move(link));
    }
    if (!toolSection.links.isEmpty()) {
        m_later.append(std::move(toolSection));
    }
}

void BlockPage::addDropFacts()
{
    Fact drops{label(u"fact.drops"_s), {}, {}};
    for (const QcItemDrop &drop : m_block.drops) {
        QStringList notes{m_text.dropNote(drop), dropCondition(drop)};
        notes.removeAll(QString());
        drops.links.append(m_links.itemLink(drop.item, notes.join(u", "_s)));
    }

    const bool needsTool = !drops.links.isEmpty() && m_block.toolLevel > 0;
    if (drops.links.isEmpty()) {
        drops.value = label(u"value.nothing"_s);
    }
    m_entry.facts.append(std::move(drops));
    if (needsTool) {
        addFact(u"withoutTool"_s, label(u"value.nothing"_s));
    }
    // An infested block lets its creature out (decision E-453).
    const std::optional<int> creature = m_block.infestsMob.isEmpty() ? std::nullopt : m_content.mobIndex(m_block.infestsMob);
    if (creature) {
        addFact(u"releases"_s, {}, {m_links.mobLink(m_content.mobs()[*creature])});
    }
}

QString BlockPage::dropCondition(const QcItemDrop &drop) const
{
    QStringList parts;
    for (const QcShapeCondition &condition : drop.when) {
        const QcBlockProperty *property = m_block.property(condition.property);
        const bool stage = property && condition.property == m_block.crop.property && !property->values.isEmpty();
        if (stage && condition.values == QStringList{property->values.constLast()}) {
            parts.append(label(u"value.whenRipe"_s));
        } else if (stage && !condition.values.contains(property->values.constLast())) {
            parts.append(label(u"value.whenGrowing"_s));
        } else {
            parts.append(condition.property + u": "_s + condition.values.join(u'/'));
        }
    }
    return parts.join(u", "_s);
}

void BlockPage::addFarmingFacts()
{
    const QcCropDefinition &crop = m_block.crop;
    if (crop.isCrop()) {
        const QcBlockProperty *stages = m_block.property(crop.property);
        const qsizetype count = stages ? stages->values.size() : 0;
        const QString wet = m_text.number(crop.averageSeconds / 60.0);
        const QString dry = m_text.number(crop.averageSeconds / std::max(crop.dryFactor, 0.01) / 60.0);
        addFact(u"crop"_s, label(u"unit.crop"_s).arg(count).arg(wet, dry));
        addFact(u"minLight"_s, label(u"unit.lightLevel"_s).arg(crop.minLight));
    }
    if (crop.bearsFruit()) {
        addFact(u"fruit"_s, label(u"unit.fruit"_s).arg(m_text.number(crop.fruitSeconds / 60.0)), {m_links.blockLink(crop.fruit)});
        QList<Link> grounds;
        for (const QString &ground : crop.fruitGround) {
            grounds.append(m_links.blockLink(ground));
        }
        addFact(u"fruitGround"_s, {}, grounds);
    }
    if (crop.plantChance > 0.0) {
        addFact(u"plantsItself"_s, m_text.percent(crop.plantChance));
    }

    addSoilFacts();
    if (!m_block.tillsInto.isEmpty()) {
        addFact(u"tillsInto"_s, {}, {m_links.blockLink(m_block.tillsInto)});
    }
    if (!m_block.flattensInto.isEmpty()) {
        addFact(u"flattensInto"_s, {}, {m_links.blockLink(m_block.flattensInto)});
    }
    if (!m_block.coveredInto.isEmpty()) {
        addFact(u"coveredInto"_s, {}, {m_links.blockLink(m_block.coveredInto)});
    }
    if (!m_block.carvesInto.isEmpty()) {
        addFact(u"carvesInto"_s, {}, {m_links.blockLink(m_block.carvesInto)});
    }
}

void BlockPage::addSoilFacts()
{
    const QcSoilDefinition &soil = m_block.soil;
    if (!soil.isSoil()) {
        return;
    }
    addFact(u"soil"_s, label(u"unit.soil"_s).arg(soil.reach), {m_links.blockLink(soil.water)});
    addFact(u"revertsTo"_s, {}, {m_links.blockLink(soil.revertsTo)});
}

void BlockPage::addPhysicalFacts()
{
    const QcBlockDefinition &block = m_block;
    if (block.lightLevel > 0) {
        addFact(u"light"_s, label(u"unit.lightLevel"_s).arg(block.lightLevel));
    }
    addLightBlockingFact();

    if (!block.solid) {
        addFact(u"solid"_s, m_text.yesNo(false));
    }
    addFlag(block.replaceable, u"replaceable"_s);
    addSupportFacts();
    if (!block.groundLiquid.isEmpty()) {
        addFact(u"groundLiquid"_s, {}, {m_links.blockLink(block.groundLiquid)});
    }
    if (block.growth.grows()) {
        const QString minutes = m_text.number(block.growth.averageSeconds / 60.0);
        addFact(u"growth"_s, label(u"unit.growth"_s).arg(block.growth.maxHeight).arg(minutes));
    }

    addFlag(block.falls, u"falls"_s);
    addFlag(block.washable, u"washable"_s);
    if (block.contactDamage > 0.0) {
        addFact(u"contactDamage"_s, m_text.healthPoints(block.contactDamage));
    }
    // Fire (decision E-392): how readily the block catches fire and burns away, eternal fire on it.
    if (block.fire.isFlammable()) {
        addFact(u"flammable"_s, label(u"unit.flammable"_s).arg(block.fire.ignite).arg(block.fire.burn));
    }
    addFlag(block.fire.eternal, u"eternalFire"_s);
    // Burning bodies and lighting fire next to it (decision E-393).
    if (block.fire.setsAlightSeconds > 0) {
        addFact(u"setsAlight"_s, m_text.seconds(block.fire.setsAlightSeconds));
    }
    if (block.fire.sparks > 0) {
        addFact(u"sparks"_s, label(u"unit.sparks"_s).arg(block.fire.sparks));
    }
    addFlag(block.climbable, u"climbable"_s);
    addMovementFacts();
}

void BlockPage::addLightBlockingFact()
{
    // Partial shapes block light only at the sides they cover (E-104); their own value has no effect.
    const QcHandbookBlockTables::LightBlocking blocking = QcHandbookBlockTables::lightBlocking(m_content, m_block);
    if (blocking.coversSides) {
        addFact(u"lightOpacity"_s, label(u"value.coveredSides"_s));
    } else if (blocking.opacity < 15) {
        addFact(u"lightOpacity"_s, label(u"unit.lightLevel"_s).arg(blocking.opacity));
    }
}

void BlockPage::addMovementFacts()
{
    const QcBlockDefinition &block = m_block;
    if (block.slowsInside < 1.0) {
        addFact(u"slowsInside"_s, m_text.percent(block.slowsInside));
    }
    if (block.surfaceSpeed != 1.0) {
        addFact(u"surfaceSpeed"_s, m_text.percent(block.surfaceSpeed));
    }
    if (block.surfaceJump != 1.0) {
        addFact(u"surfaceJump"_s, m_text.percent(block.surfaceJump));
    }
    // Slippery ground (decision E-430): the share of their speed walkers keep per tick on it.
    if (block.slipperiness > QcPhysics::grippingSlipperiness) {
        addFact(u"slipperiness"_s, m_text.percent(QcPhysics::slideKeep(block.slipperiness, QcPhysics::footDrag, QcPhysics::tickDuration)));
    }
    if (!block.hardensWith.isEmpty()) {
        m_entry.facts.append({label(u"fact.hardens"_s).arg(m_links.itemTitle(block.hardensWith, m_text.language())), {},
                              {m_links.blockLink(block.hardensInto)}});
    }
}

void BlockPage::addSupportFacts()
{
    const QcBlockDefinition &block = m_block;
    if (block.support == QcBlockSupport::None && block.groundBlocks.isEmpty()) {
        return;
    }

    Fact support{label(u"fact.support"_s), {}, {}};
    if (!block.groundBlocks.isEmpty()) {
        support.value = label(u"value.supportGround"_s);
        for (const QString &ground : block.groundBlocks) {
            support.links.append(m_links.blockLink(ground));
        }
    } else {
        support.value = label(block.support == QcBlockSupport::Below ? u"value.supportBelow"_s : u"value.supportAttached"_s);
    }
    m_entry.facts.append(std::move(support));
}

void BlockPage::addUseFacts()
{
    const QcBlockDefinition &block = m_block;
    addFlag(block.toggles, u"toggles"_s);
    addFlag(block.door, u"door"_s);
    if (block.opensCrafting) {
        addFact(u"crafting"_s, label(u"value.craftingGrid"_s));
    }
    if (block.containerSlots > 0) {
        addFact(u"storage"_s, label(u"unit.slots"_s).arg(block.containerSlots));
    }
    if (block.machine.isMachine()) {
        addMachineFacts();
    }
    if (block.fuelSeconds > 0.0) {
        addFact(u"fuel"_s, m_text.seconds(block.fuelSeconds), {m_links.tableLink(u"fuels"_s)});
    }
}

void BlockPage::addMachineFacts()
{
    const QcMachineDefinition &machine = m_block.machine;
    addFact(u"machine"_s, label(u"value.machine"_s).arg(m_text.machineName(machine.category), m_text.number(machine.speed)),
            {m_links.tableLink(u"fuels"_s)});

    Section processes{label(u"section.processes"_s), {}, {}, {}};
    for (const QcSmeltingRecipe &recipe : m_content.smeltingRecipes()) {
        if (recipe.category == machine.category) {
            processes.recipes.append(m_recipes.machineRecipe(recipe));
        }
    }
    if (!processes.recipes.isEmpty()) {
        m_later.append(std::move(processes));
    }
}

void BlockPage::addSaplingFacts()
{
    const QcSaplingDefinition &sapling = m_block.sapling;
    if (!sapling.isSapling()) {
        return;
    }

    const auto feature = std::ranges::find(m_content.worldgen().features(), sapling.feature, &QcFeatureDefinition::name);
    Fact grows{label(u"fact.growsInto"_s),
               label(u"unit.secondsRange"_s).arg(m_text.number(sapling.minSeconds), m_text.number(sapling.maxSeconds)),
               {}};
    if (feature != m_content.worldgen().features().end()) {
        grows.links.append(m_links.featureLink(*feature));
    }
    m_entry.facts.append(std::move(grows));

    if (!sapling.plantOn.isEmpty()) {
        Fact plantOn{label(u"fact.plantOn"_s), {}, {}};
        for (const QString &ground : sapling.plantOn) {
            plantOn.links.append(m_links.blockLink(ground));
        }
        m_entry.facts.append(std::move(plantOn));
    }
    if (sapling.plantChance > 0.0) {
        addFact(u"plantChance"_s, m_text.percent(sapling.plantChance));
    }
}

void BlockPage::addPartFacts()
{
    const QcBlockDefinition &block = m_block;
    if (!block.partOf.isEmpty()) {
        const QString material = block.partOf.section(u'/', 0, 0);
        const QString family = block.partOf.section(u'/', 1);
        const auto base = std::ranges::find(m_content.materials(), material, &QcMaterialDefinition::name);
        if (base != m_content.materials().end()) {
            addFact(u"madeFrom"_s, {}, {m_links.blockLink(base->block)});
        }
        const QString familyTitle = m_text.lookup(u"handbook.family."_s + QcHandbookLinks::localPart(family));
        addFact(u"family"_s, familyTitle.isEmpty() ? QcHandbookLinks::humanized(family) : familyTitle);
    }

    if (!block.connectGroup.isEmpty() || !block.connectsTo.isEmpty()) {
        QStringList groups;
        for (const QString &group : block.connectsTo) {
            const QString title = m_text.lookup(u"handbook.group."_s + QcHandbookLinks::localPart(group));
            groups.append(title.isEmpty() ? QcHandbookLinks::humanized(group) : title);
        }
        groups.append(label(u"value.fullBlocks"_s));
        addFact(u"connects"_s, groups.join(u", "_s));
    }
}

void BlockPage::addDescriptiveFacts()
{
    const QcBlockDefinition &block = m_block;
    if (!block.soundMaterial.isEmpty()) {
        const QString sound = m_text.lookup(u"handbook.sound."_s + QcHandbookLinks::localPart(block.soundMaterial));
        addFact(u"sound"_s, sound.isEmpty() ? QcHandbookLinks::humanized(block.soundMaterial) : sound);
    }

    if (!block.properties.isEmpty()) {
        QStringList states;
        for (const QcBlockProperty &property : block.properties) {
            states.append(u"%1 (%2)"_s.arg(property.name, property.values.join(u", "_s)));
        }
        addFact(u"states"_s, states.join(u"; "_s));
    }

    if (!block.itemTags.isEmpty()) {
        Fact tags{label(u"fact.tags"_s), {}, {}};
        for (const QString &tag : block.itemTags) {
            tags.links.append(m_links.tagLink(tag));
        }
        m_entry.facts.append(std::move(tags));
    }
}

void BlockPage::addSections()
{
    const QString &name = m_block.name;
    if (const QStringList partNames = m_index.partsByBase(name); !partNames.isEmpty()) {
        Section parts{label(u"section.parts"_s), {}, {}, {}};
        for (const QString &part : partNames) {
            parts.links.append(m_links.blockLink(part));
        }
        parts.links = QcHandbookLinks::sortedLinks(std::move(parts.links));
        m_entry.sections.append(std::move(parts));
    }

    m_entry.sections += m_recipes.itemSections(name);
    m_entry.sections += m_later;

    const QList<Link> found = QcHandbookLinks::sortedLinks(m_index.foundIn(name));
    if (!found.isEmpty()) {
        m_entry.sections.append({label(u"section.foundIn"_s), {}, found, {}});
    }

    const QList<Link> spawners = QcHandbookLinks::sortedLinks(m_index.spawnersOn(name));
    if (!spawners.isEmpty()) {
        m_entry.sections.append({label(u"section.spawnsOn"_s), {}, spawners, {}});
    }
}

} // namespace

namespace QcHandbookBlockPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context)
{
    QList<QcHandbookEntry> entries;
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (block.renderLayer != QcRenderLayer::Invisible) {
            entries.append(BlockPage(context, block).build());
        }
    }
    return entries;
}

} // namespace QcHandbookBlockPages
