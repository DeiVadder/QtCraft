#include "common/handbook/builder/QcHandbookCreaturePages.h"

#include "common/core/QcWorldConstants.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"

using namespace Qt::StringLiterals;
using Fact = QcHandbookEntry::Fact;
using Section = QcHandbookEntry::Section;

namespace {

// "ambient", "hostile" or "passive": the behaviour label and the rule of the category's cap.
QString kindOf(const QcMobDefinition &mob)
{
    return mob.ambient ? u"ambient"_s : (mob.hostile ? u"hostile"_s : u"passive"_s);
}

// Behaviour, health, size, speeds and how it attacks.
void addBodyFacts(const QcHandbookText &text, const QcMobDefinition &mob, QList<Fact> &facts)
{
    facts.append({text.label(u"fact.behaviour"_s), text.label(u"value.mob."_s + kindOf(mob)), {}});
    facts.append({text.label(u"fact.health"_s), text.healthPoints(mob.maxHealth), {}});
    facts.append({text.label(u"fact.size"_s), text.label(u"unit.size"_s).arg(text.number(mob.width), text.number(mob.height)), {}});

    // Hostile creatures run at chaseSpeed after players, the others when they flee.
    const QString speeds = mob.chaseSpeed == mob.walkSpeed
                               ? text.label(u"unit.metresPerSecond"_s).arg(text.number(mob.walkSpeed))
                               : text.label(mob.hostile ? u"unit.speeds"_s : u"unit.speedsFleeing"_s)
                                     .arg(text.number(mob.walkSpeed), text.number(mob.chaseSpeed));
    facts.append({text.label(u"fact.speed"_s), speeds, {}});
    if (mob.hostile) {
        facts.append({text.label(u"fact.attack"_s),
                      text.label(u"value.attack"_s)
                          .arg(text.number(mob.attackDamage), text.number(mob.attackRange), text.number(mob.attackCooldown)),
                      {}});
        facts.append({text.label(u"fact.sight"_s), text.label(u"unit.metres"_s).arg(text.number(mob.sightRange)), {}});
    }
}

// The creature's own cap and the cap of its category from the rules.
void addCapFact(const QcHandbookText &text, const QcMobDefinition &mob, QList<Fact> &facts)
{
    const QString kind = kindOf(mob);
    if (const std::optional<double> cap = text.rule(u"mobs.%1Cap"_s.arg(kind))) {
        const QString categoryCap = text.label(u"value.categoryCap."_s + kind).arg(text.number(*cap));
        facts.append({text.label(u"fact.cap"_s),
                      mob.maxCount > 0 ? text.label(u"value.cap"_s).arg(mob.maxCount) + u"; "_s + categoryCap : categoryCap, {}});
    } else if (mob.maxCount > 0) {
        facts.append({text.label(u"fact.cap"_s), text.label(u"value.cap"_s).arg(mob.maxCount), {}});
    }
}

// When, where, how many and how often it spawns.
void addSpawnFacts(const QcHandbookText &text, const QcMobDefinition &mob, QList<Fact> &facts)
{
    if (!mob.spawnsNaturally()) {
        facts.append({text.label(u"fact.spawnTime"_s), text.label(u"value.time.never"_s), {}});
        return;
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
    addCapFact(text, mob, facts);
}

// Flying, glowing, catching and drops.
void addTraitFacts(const QcHandbookContext &context, const QcMobDefinition &mob, QList<Fact> &facts)
{
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

    Fact drops{text.label(u"fact.drops"_s), {}, {}};
    for (const QcItemDrop &drop : mob.drops) {
        drops.links.append(links.itemLink(drop.item, text.dropNote(drop)));
    }
    if (drops.links.isEmpty()) {
        drops.value = text.label(u"value.nothing"_s);
    }
    facts.append(std::move(drops));
}

// The blocks and biomes it spawns on.
void addSpawnSections(const QcHandbookContext &context, const QcMobDefinition &mob, QList<Section> &sections)
{
    const QcHandbookText &text = context.text;
    if (!mob.spawnsNaturally()) {
        return;
    }

    Section spawnsOn{text.label(u"section.spawnBlocks"_s), {}, {}, {}};
    for (const QString &block : mob.spawnBlocks) {
        spawnsOn.links.append(context.links.blockLink(block));
    }
    if (spawnsOn.links.isEmpty()) {
        spawnsOn.text = text.label(mob.flying ? u"value.anySolidOrLiquid"_s : u"value.anySolid"_s);
    }
    QcHandbookLinks::sortLinks(spawnsOn.links);
    sections.append(std::move(spawnsOn));

    Section biomes{text.label(u"section.spawnBiomes"_s), {}, {}, {}};
    for (const QcBiomeDefinition &biome : context.content.biomes()) {
        if (mob.spawnBiomes.contains(biome.name)) {
            biomes.links.append(context.links.biomeLink(biome));
        }
    }
    QcHandbookLinks::sortLinks(biomes.links);
    if (mob.spawnBiomes.isEmpty()) {
        biomes.text = text.label(u"value.allBiomes"_s);
    }
    sections.append(std::move(biomes));
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
    for (const QcBiomeDefinition &biome : context.content.biomes()) {
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
    entry.title = context.content.translate(mob.translationKey, text.language());
    entry.icon = mob.drops.isEmpty() ? links.itemIcon(mob.catchItem) : links.itemIcon(mob.drops.first().item);
    entry.text = text.description(mob.descriptionKey, mob.name);
    entry.facts.append({text.label(u"fact.id"_s), mob.name, {}});

    addBodyFacts(text, mob, entry.facts);
    addSpawnFacts(text, mob, entry.facts);
    addTraitFacts(context, mob, entry.facts);

    addSpawnSections(context, mob, entry.sections);
    for (const QcMobDefinition::SpawnRule &rule : mob.extraSpawns) {
        entry.sections.append(extraSpawnSection(context, rule));
    }

    entry.englishTitle = context.content.translate(mob.translationKey, QcHandbookText::english());
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
