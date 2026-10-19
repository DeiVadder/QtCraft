#include "common/content/registry/QcSpawnerContent.h"

#include "common/content/registry/QcContentRegistry.h"

#include <QSet>

using namespace Qt::StringLiterals;

namespace {

// Why a spawner may not bring forth the kind named `name`; empty when it may.
QString problemWith(const QcContentRegistry &content, const QString &name)
{
    const std::optional<int> index = content.mobIndex(name);
    if (!index) {
        return u"unknown creature '%1'"_s.arg(name);
    }
    const QString refusal = QcSpawnerContent::refusal(content.mobs()[*index]);
    return refusal.isEmpty() ? QString() : u"creature '%1' %2"_s.arg(name, refusal);
}

QStringList resolveDungeons(QcContentRegistry &content)
{
    QStringList warnings;
    QcTerrainSettings settings = content.terrainSettings();
    QcDungeonSettings &dungeons = settings.dungeons;
    if (!dungeons.enabled) {
        return warnings;
    }

    const qsizetype before = dungeons.mobs.size();
    dungeons.mobs.removeIf([&](const QcDungeonSettings::Mob &mob) {
        const QString problem = problemWith(content, mob.mob);
        if (!problem.isEmpty()) {
            warnings.append(u"dungeons: %1, left out"_s.arg(problem));
        }
        return !problem.isEmpty();
    });
    if (!dungeons.lootTable.isEmpty() && !content.lootTables().find(dungeons.lootTable)) {
        warnings.append(u"dungeons: unknown loot table '%1', the chests stay empty"_s.arg(dungeons.lootTable));
    }
    if (dungeons.mobs.size() != before) {
        content.setTerrainSettings(settings);
    }
    return warnings;
}

// The mine shafts' creature, loot table and cart (decision E-570); a creature a spawner may not bring
// forth leaves their spawners out.
QStringList resolveMineshafts(QcContentRegistry &content)
{
    QStringList warnings;
    QcTerrainSettings settings = content.terrainSettings();
    QcMineshaftSettings &shafts = settings.mineshafts;
    if (!shafts.enabled) {
        return warnings;
    }

    if (const QString problem = problemWith(content, shafts.mob); !problem.isEmpty()) {
        warnings.append(u"mineshafts: %1, no spawners"_s.arg(problem));
        shafts.maxSpawners = 0;
        shafts.minSpawners = 0;
        content.setTerrainSettings(settings);
    }
    if (!shafts.lootTable.isEmpty() && !content.lootTables().find(shafts.lootTable)) {
        warnings.append(u"mineshafts: unknown loot table '%1', the carts stay empty"_s.arg(shafts.lootTable));
    }
    const std::optional<QcItemId> cart = content.itemId(shafts.cart);
    if (!cart || !content.item(*cart).placesCart) {
        warnings.append(u"mineshafts: '%1' is no minecart item, no carts"_s.arg(shafts.cart));
    }
    for (const QcStructureDefinition &structure : content.structures()) {
        const int most = QcMineshaftSettings::maxRoomFootprint;
        if (structure.mineshaftWeight > 0 && !shafts.fitsRoom(structure.sizeX, structure.sizeY, structure.sizeZ)) {
            warnings.append(u"structure '%1': a mine shaft room needs odd sides of at most %2 and a height below %3, it is left out"_s.arg(structure.name)
                                .arg(most)
                                .arg(shafts.levelSpacing));
        }
    }
    return warnings;
}

QStringList checkStructures(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcStructureDefinition &structure : content.structures()) {
        for (const QcStructureDefinition::Spawner &spawner : structure.spawners) {
            if (const QString problem = problemWith(content, spawner.mob); !problem.isEmpty()) {
                warnings.append(u"structure '%1': spawner with %2, it spawns nothing"_s.arg(structure.name, problem));
            }
        }
        // Guardian zones (decision E-591) bring forth the same kinds a spawner may.
        for (const QcStructureDefinition::Guardian &guardian : structure.guardians) {
            if (const QString problem = problemWith(content, guardian.mob); !problem.isEmpty()) {
                warnings.append(u"structure '%1': guardians with %2, none come"_s.arg(structure.name, problem));
            }
        }
        for (const QcStructureDefinition::Loot &loot : structure.loot) {
            if (!content.lootTables().find(loot.table)) {
                warnings.append(u"structure '%1': unknown loot table '%2', the chest stays empty"_s.arg(structure.name, loot.table));
            }
        }
    }
    return warnings;
}

QStringList checkBlocks(const QcContentRegistry &content)
{
    QStringList warnings;
    QSet<QString> checked;
    for (qsizetype id = 0; id < content.stateCount(); ++id) {
        const QcBlockDefinition &block = content.block(QcBlockId(id));
        if (block.spawner.isSpawner && !block.spawner.mob.isEmpty() && !checked.contains(block.name)) {
            checked.insert(block.name);
            if (const QString problem = problemWith(content, block.spawner.mob); !problem.isEmpty()) {
                warnings.append(u"block '%1': spawner with %2, it spawns nothing"_s.arg(block.name, problem));
            }
        }
    }
    return warnings;
}

QStringList checkLoot(const QcContentRegistry &content)
{
    QStringList warnings;
    const auto checkPool = [&](const QcLootTableDefinition &table, const QcLootTableDefinition::Pool &pool) {
        for (const QcLootTableDefinition::Entry &entry : pool.entries) {
            if (!entry.item.isEmpty() && !content.itemId(entry.item)) {
                warnings.append(u"loot table '%1': unknown item '%2', it gives nothing"_s.arg(table.name, entry.item));
            }
        }
    };
    for (const QcLootTableDefinition &table : content.lootTables().all()) {
        for (const QcLootTableDefinition::Pool &pool : table.pools) {
            checkPool(table, pool);
        }
    }
    return warnings;
}

} // namespace

namespace QcSpawnerContent {

QString refusal(const QcMobDefinition &mob)
{
    if (mob.villager) {
        return u"is a villager"_s;
    }
    if (mob.aquatic) {
        return u"lives in water"_s;
    }
    return {};
}

QStringList resolve(QcContentRegistry &content)
{
    QStringList warnings = resolveDungeons(content);
    warnings.append(resolveMineshafts(content));
    warnings.append(checkStructures(content));
    warnings.append(checkBlocks(content));
    warnings.append(checkLoot(content));
    return warnings;
}

} // namespace QcSpawnerContent
