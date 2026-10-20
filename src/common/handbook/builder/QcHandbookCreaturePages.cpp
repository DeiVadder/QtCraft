#include "common/handbook/builder/QcHandbookCreaturePages.h"

#include "common/core/QcWorldConstants.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;
using Fact = QcHandbookEntry::Fact;
using Section = QcHandbookEntry::Section;

namespace {

// "ambient", "hostile" or "passive": the behaviour label and the rule of the category's cap.
QString kindOf(const QcMobDefinition &mob)
{
    return mob.ambient ? u"ambient"_s : (mob.hostile ? u"hostile"_s : u"passive"_s);
}

// The behaviour label: the category, "archer" for hostile creatures that shoot (decision E-426) or
// "neutral" for those a player has to provoke (decision E-460; "neutralGaze" for those a look at
// their head provokes too), "golem" for the protectors of villages (decision E-471) or "guardian"
// for creatures that keep to a zone they guard (decision E-594).
QString behaviourOf(const QcMobDefinition &mob)
{
    if (mob.ai == QcMobDefinition::Ai::Neutral) {
        return mob.gaze ? u"neutralGaze"_s : u"neutral"_s;
    }
    if (mob.ai == QcMobDefinition::Ai::Golem) {
        return u"golem"_s;
    }
    if (mob.ai == QcMobDefinition::Ai::Guardian) {
        return u"guardian"_s;
    }
    return mob.ai == QcMobDefinition::Ai::Archer ? u"archer"_s : kindOf(mob);
}

// Behaviour, health, size, speeds and how it attacks.
[[nodiscard]] QList<Fact> bodyFacts(const QcHandbookText &text, const QcMobDefinition &mob)
{
    QList<Fact> facts;
    facts.append({text.label(u"fact.behaviour"_s), text.label(u"value.mob."_s + behaviourOf(mob)), {}});
    facts.append({text.label(u"fact.health"_s), text.healthPoints(mob.maxHealth), {}});
    facts.append({text.label(u"fact.size"_s), text.label(u"unit.size"_s).arg(text.number(mob.width), text.number(mob.height)), {}});

    // Hostile creatures and golems run at chaseSpeed after their targets, the others when they flee.
    const QString speeds = mob.chaseSpeed == mob.walkSpeed
                               ? text.label(u"unit.metresPerSecond"_s).arg(text.number(mob.walkSpeed))
                               : text.label(mob.hostile || mob.ai == QcMobDefinition::Ai::Golem ? u"unit.speeds"_s : u"unit.speedsFleeing"_s)
                                     .arg(text.number(mob.walkSpeed), text.number(mob.chaseSpeed));
    facts.append({text.label(u"fact.speed"_s), speeds, {}});
    if (mob.hostile || mob.ai == QcMobDefinition::Ai::Golem) {
        // A creature without blows (the witch only throws, decision E-623) has no attack row.
        if (mob.attackDamage > 0.0) {
            facts.append({text.label(u"fact.attack"_s),
                          text.label(u"value.attack"_s)
                              .arg(text.number(mob.attackDamage), text.number(mob.attackRange), text.number(mob.attackCooldown)),
                          {}});
        }
        facts.append({text.label(u"fact.sight"_s), text.label(u"unit.metres"_s).arg(text.number(mob.sightRange)), {}});
    }
    return facts;
}

// The creature's own cap and the cap of its category from the rules; std::nullopt when it has neither.
[[nodiscard]] std::optional<Fact> capFact(const QcHandbookText &text, const QcMobDefinition &mob)
{
    const QString kind = kindOf(mob);
    if (const std::optional<double> cap = text.rule(u"mobs.%1Cap"_s.arg(kind))) {
        const QString categoryCap = text.label(u"value.categoryCap."_s + kind).arg(text.number(*cap));
        return Fact{text.label(u"fact.cap"_s), mob.maxCount > 0 ? text.label(u"value.cap"_s).arg(mob.maxCount) + u"; "_s + categoryCap : categoryCap, {}};
    }
    if (mob.maxCount > 0) {
        return Fact{text.label(u"fact.cap"_s), text.label(u"value.cap"_s).arg(mob.maxCount), {}};
    }
    return std::nullopt;
}

// Whether the pieces of the village style place the creature or the style names it among its residents.
bool placesIn(const QcContentRegistry &content, const QcVillageStyleDefinition &style, const QcMobDefinition &mob)
{
    const std::optional<int> self = content.mobIndex(mob.name);
    const auto isSelf = [&](const QString &name) { return content.mobIndex(name) == self; };
    for (const QcVillagePiece &piece : style.pieces) {
        for (const QcVillageBlueprint &blueprint : piece.blueprints) {
            const QcStructureDefinition *structure = content.worldgen().structure(blueprint.structure);
            if (structure && std::ranges::any_of(structure->mobs, isSelf, &QcStructureDefinition::Mob::mob)) {
                return true;
            }
        }
    }
    return std::ranges::any_of(style.residents, isSelf, &QcVillageResident::mob);
}

// The village styles that place the creature (placesIn).
QList<const QcVillageStyleDefinition *> villageStylesOf(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QList<const QcVillageStyleDefinition *> styles;
    for (const QcVillageStyleDefinition &style : content.villageCatalog().styles()) {
        if (placesIn(content, style, mob)) {
            styles.append(&style);
        }
    }
    return styles;
}

// When, where, how many and how often it spawns.
[[nodiscard]] QList<Fact> spawnFacts(const QcHandbookContext &context, const QcMobDefinition &mob)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    if (!mob.spawnsNaturally()) {
        const bool villages = !villageStylesOf(context.content, mob).isEmpty();
        facts.append({text.label(u"fact.spawnTime"_s), text.label(villages ? u"value.time.villages"_s : u"value.time.never"_s), {}});
        return facts;
    }

    const QString time = mob.spawnTime == QcMobDefinition::SpawnTime::Day     ? u"day"_s
                         : mob.spawnTime == QcMobDefinition::SpawnTime::Night ? u"night"_s
                                                                              : u"any"_s;
    facts.append({text.label(u"fact.spawnTime"_s), text.label(u"value.time."_s + time), {}});

    if (mob.spawnsInCaves) {
        facts.append({text.label(u"fact.caves"_s), text.label(u"value.caves"_s), {}});
    }
    if (mob.maxSpawnLight < QcBlockDefinition::maxLightLevel) {
        facts.append({text.label(u"fact.spawnLight"_s), text.label(u"value.maxLight"_s).arg(mob.maxSpawnLight), {}});
    }
    if (mob.spawnMaxY < QcWorld::worldHeight) {
        facts.append({text.label(u"fact.spawnMaxY"_s), text.label(u"value.belowY"_s).arg(mob.spawnMaxY), {}});
    }
    if (mob.spawnChunkChance < 1.0) {
        facts.append({text.label(u"fact.spawnChunks"_s), text.label(u"value.spawnChunks"_s).arg(text.number(100.0 * mob.spawnChunkChance)), {}});
    }
    if (!mob.hostile && !mob.ambient) {
        facts.append({text.label(u"fact.persistent"_s), text.label(u"value.persistent"_s), {}});
    }

    facts.append({text.label(u"fact.group"_s), text.label(u"value.group"_s).arg(mob.maxGroup), {}});
    facts.append({text.label(u"fact.weight"_s), text.number(mob.spawnWeight), {}});
    if (std::optional<Fact> cap = capFact(text, mob)) {
        facts.append(std::move(*cap));
    }
    return facts;
}

// Flying, glowing, catching and drops.
[[nodiscard]] QList<Fact> traitFacts(const QcHandbookContext &context, const QcMobDefinition &mob)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;

    if (mob.flying) {
        facts.append({text.label(u"fact.flying"_s), text.yesNo(true), {}});
    }
    if (mob.glow > 0) {
        facts.append({text.label(u"fact.glow"_s), text.label(u"unit.lightLevel"_s).arg(mob.glow), {}});
    }
    if (!mob.catchWith.isEmpty()) {
        facts.append({text.label(u"fact.catch"_s), {}, {links.itemLink(mob.catchWith), links.itemLink(mob.catchItem)}});
    }
    if (mob.breeding.breeds()) {
        QList<QcHandbookEntry::Link> food;
        for (const QString &item : mob.breeding.food) {
            food.append(links.itemLink(item));
        }
        facts.append({text.label(u"fact.breedsWith"_s), {}, food});
        facts.append({text.label(u"fact.breeding"_s),
                      text.label(u"unit.breeding"_s)
                          .arg(text.number(mob.breeding.cooldownSeconds / 60.0), text.number(mob.breeding.growUpSeconds / 60.0)),
                      {}});
    }
    if (mob.laying.lays()) {
        facts.append({text.label(u"fact.lays"_s),
                      text.label(u"unit.lays"_s).arg(text.number(mob.laying.minSeconds / 60.0), text.number(mob.laying.maxSeconds / 60.0)),
                      {links.itemLink(mob.laying.item)}});
    }

    Fact drops{text.label(u"fact.drops"_s), {}, {}};
    for (const QcItemDrop &drop : mob.drops) {
        drops.links.append(links.itemLink(drop.item, text.dropNote(drop)));
    }
    if (drops.links.isEmpty()) {
        drops.value = text.label(u"value.nothing"_s);
    }
    facts.append(std::move(drops));
    return facts;
}

// Taming, what a pet does, shyness and packs (decision E-480).
[[nodiscard]] QList<Fact> companionFacts(const QcHandbookContext &context, const QcMobDefinition &mob)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    if (mob.tameable) {
        const QcTameDefinition &tame = *mob.tameable;
        if (!tame.items.isEmpty()) {
            Fact tamedWith{text.label(u"fact.tamedWith"_s), text.label(u"value.tameChance"_s).arg(text.number(100.0 * tame.chance)), {}};
            for (const QString &item : tame.items) {
                tamedWith.links.append(context.links.itemLink(item));
            }
            facts.append(std::move(tamedWith));
        }
        // What a pet does is the kind's it becomes (a wolf becomes a dog).
        const QcMobDefinition &petKind = tame.becomesType >= 0 ? context.content.mobs()[tame.becomesType] : mob;
        const QcTameDefinition &pet = petKind.tameable ? *petKind.tameable : tame;
        if (tame.becomesType >= 0) {
            facts.append({text.label(u"fact.becomes"_s), {}, {context.links.mobLink(petKind)}});
        }
        facts.append({text.label(u"fact.pet"_s), text.label(pet.defends ? u"value.pet.defends"_s : u"value.pet.follows"_s), {}});
        if (!tame.healItems.isEmpty()) {
            Fact heals{text.label(u"fact.healedBy"_s), {}, {}};
            for (const QString &item : tame.healItems) {
                heals.links.append(context.links.itemLink(item));
            }
            facts.append(std::move(heals));
        }
        if (pet.hasCollar()) {
            facts.append({text.label(u"fact.collar"_s), text.label(u"value.collar"_s), {context.links.itemLink(pet.collarDefault)}});
        }
    }
    if (mob.shy) {
        Fact shy{text.label(u"fact.shy"_s), text.label(u"value.shy"_s).arg(text.number(mob.shy->distance)), {}};
        for (const QString &item : mob.shy->calmItems) {
            shy.links.append(context.links.itemLink(item));
        }
        facts.append(std::move(shy));
    }
    if (mob.packRange > 0.0) {
        facts.append({text.label(u"fact.pack"_s), text.label(u"value.pack"_s).arg(text.number(mob.packRange)), {}});
    }
    return facts;
}

// The blocks and biomes it spawns on.
[[nodiscard]] QList<Section> spawnSections(const QcHandbookContext &context, const QcMobDefinition &mob)
{
    QList<Section> sections;
    const QcHandbookText &text = context.text;
    if (!mob.spawnsNaturally()) {
        return sections;
    }

    Section spawnsOn{text.label(u"section.spawnBlocks"_s), {}, {}, {}};
    for (const QString &block : mob.spawnBlocks) {
        spawnsOn.links.append(context.links.blockLink(block));
    }
    if (spawnsOn.links.isEmpty()) {
        spawnsOn.text = text.label(mob.flying ? u"value.anySolidOrLiquid"_s : u"value.anySolid"_s);
    }
    spawnsOn.links = QcHandbookLinks::sortedLinks(std::move(spawnsOn.links));
    sections.append(std::move(spawnsOn));

    Section biomes{text.label(u"section.spawnBiomes"_s), {}, {}, {}};
    for (const QcBiomeDefinition &biome : context.content.worldgen().biomes()) {
        if (mob.spawnBiomes.contains(biome.name)) {
            biomes.links.append(context.links.biomeLink(biome));
        }
    }
    biomes.links = QcHandbookLinks::sortedLinks(std::move(biomes.links));
    if (mob.spawnBiomes.isEmpty()) {
        biomes.text = text.label(u"value.allBiomes"_s);
    }
    sections.append(std::move(biomes));
    return sections;
}

// The biomes of the village styles that place it (a cat in the large houses); std::nullopt when
// none does.
[[nodiscard]] std::optional<Section> villageSection(const QcHandbookContext &context, const QcMobDefinition &mob)
{
    const QList<const QcVillageStyleDefinition *> styles = villageStylesOf(context.content, mob);
    if (styles.isEmpty()) {
        return std::nullopt;
    }

    Section villages{context.text.label(u"section.villages"_s), {}, {}, {}};
    for (const QcBiomeDefinition &biome : context.content.worldgen().biomes()) {
        if (std::ranges::any_of(styles, [&](const QcVillageStyleDefinition *style) { return style->biomes.contains(biome.name); })) {
            villages.links.append(context.links.biomeLink(biome));
        }
    }
    villages.links = QcHandbookLinks::sortedLinks(std::move(villages.links));
    if (std::ranges::any_of(styles, &QcVillageStyleDefinition::fallback)) {
        villages.text = context.text.label(u"value.villageFallback"_s);
    }
    return villages;
}

// Further places and times (slimes on swamp surfaces at night): conditions as text, biomes and
// blocks as links.
Section extraSpawnSection(const QcHandbookContext &context, const QcMobDefinition::SpawnRule &rule)
{
    const QcHandbookText &text = context.text;
    const QString ruleTime = rule.time == QcMobDefinition::SpawnTime::Day     ? u"day"_s
                             : rule.time == QcMobDefinition::SpawnTime::Night ? u"night"_s
                                                                              : u"any"_s;
    QStringList conditions{text.label(u"value.time."_s + ruleTime)};

    if (rule.caves) {
        conditions.append(text.label(u"value.caves"_s));
    }
    if (rule.maxY < QcWorld::worldHeight) {
        conditions.append(text.label(u"value.belowY"_s).arg(rule.maxY));
    }
    if (rule.maxLight < QcBlockDefinition::maxLightLevel) {
        conditions.append(text.label(u"value.maxLight"_s).arg(rule.maxLight));
    }
    if (rule.chunkChance < 1.0) {
        conditions.append(text.label(u"value.spawnChunks"_s).arg(text.number(100.0 * rule.chunkChance)));
    }

    Section also{text.label(u"section.alsoSpawns"_s), conditions.join(u"; "_s), {}, {}};
    for (const QcBiomeDefinition &biome : context.content.worldgen().biomes()) {
        if (rule.biomes.contains(biome.name)) {
            also.links.append(context.links.biomeLink(biome));
        }
    }
    for (const QString &block : rule.blocks) {
        also.links.append(context.links.blockLink(block));
    }
    if (rule.biomes.isEmpty()) {
        also.text += u"; "_s + text.label(u"value.allBiomes"_s);
    }
    return also;
}

QcHandbookEntry creaturePage(const QcHandbookContext &context, const QcMobDefinition &mob)
{
    QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;

    QcHandbookEntry entry;
    entry.id = u"mob:"_s + mob.name;
    entry.name = mob.name;
    entry.category = u"creatures"_s;
    entry.pack = QcHandbookLinks::packOf(mob.name);
    entry.title = context.content.translations().translate(mob.translationKey, text.language());
    entry.icon = mob.drops.isEmpty() ? links.itemIcon(mob.catchItem) : links.itemIcon(mob.drops.first().item);
    entry.text = text.description(mob.descriptionKey, mob.name);
    entry.facts.append({text.label(u"fact.id"_s), mob.name, {}});

    entry.facts += bodyFacts(text, mob);
    entry.facts += spawnFacts(context, mob);
    entry.facts += traitFacts(context, mob);
    entry.facts += companionFacts(context, mob);

    entry.sections += spawnSections(context, mob);
    if (std::optional<Section> villages = villageSection(context, mob)) {
        entry.sections.append(std::move(*villages));
    }
    for (const QcMobDefinition::SpawnRule &rule : mob.extraSpawns) {
        entry.sections.append(extraSpawnSection(context, rule));
    }

    entry.englishTitle = context.content.translations().translate(mob.translationKey, QcHandbookText::english());
    return entry;
}

} // namespace

namespace QcHandbookCreaturePages {

QList<QcHandbookEntry> build(const QcHandbookContext &context)
{
    QList<QcHandbookEntry> entries;
    for (const QcMobDefinition &mob : context.content.mobs()) {
        entries.append(creaturePage(context, mob));
    }
    return entries;
}

} // namespace QcHandbookCreaturePages
