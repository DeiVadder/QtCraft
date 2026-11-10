// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/blockbehaviour/QcSpawnerContent.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcGeneratorVersion.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Why a spawner may not bring forth the kind named `name`; empty when it may.
QString problemWith(const QcContentRegistry &content, const QString &name)
{
    const std::optional<QcCreatureKindId> index = content.mobKind(name);
    if (!index) {
        return u"unknown creature '%1'"_s.arg(name);
    }
    const QString refusal = QcSpawnerContent::refusal(content.mobs()[*index]);
    return refusal.isEmpty() ? QString() : u"creature '%1' %2"_s.arg(name, refusal);
}

QStringList resolveDungeons(QcContentRegistry &content)
{
    QStringList warnings;
    QcTerrainSettings settings = content.worldgen().terrainSettings();
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
        content.worldgen().setTerrainSettings(settings);
    }
    return warnings;
}

// The mine shafts' creature, loot table and cart (decision E-570); a creature a spawner may not bring
// forth leaves their spawners out.
QStringList resolveMineshafts(QcContentRegistry &content)
{
    QStringList warnings;
    QcTerrainSettings settings = content.worldgen().terrainSettings();
    QcMineshaftSettings &shafts = settings.mineshafts;
    if (!shafts.enabled) {
        return warnings;
    }

    if (const QString problem = problemWith(content, shafts.mob); !problem.isEmpty()) {
        warnings.append(u"mineshafts: %1, no spawners"_s.arg(problem));
        shafts.maxSpawners = 0;
        shafts.minSpawners = 0;
        content.worldgen().setTerrainSettings(settings);
    }
    if (!shafts.lootTable.isEmpty() && !content.lootTables().find(shafts.lootTable)) {
        warnings.append(u"mineshafts: unknown loot table '%1', the carts stay empty"_s.arg(shafts.lootTable));
    }
    const std::optional<QcItemId> cart = content.itemId(shafts.cart);
    if (!cart || !content.item(*cart).placesCart) {
        warnings.append(u"mineshafts: '%1' is no minecart item, no carts"_s.arg(shafts.cart));
    }
    for (const QcStructureDefinition &structure : content.worldgen().structures()) {
        const int most = QcMineshaftSettings::maxRoomFootprint;
        if (structure.mineshaftWeight > 0 && !shafts.fitsRoom(structure.sizeX, structure.sizeY, structure.sizeZ)) {
            warnings.append(u"structure '%1': a mine shaft room needs odd sides of at most %2 and a height below %3, it is left out"_s.arg(structure.name)
                                .arg(most)
                                .arg(shafts.levelSpacing));
        }
    }
    return warnings;
}

// The loot tables of the chests of `structure`, in every generator version.
QStringList checkChests(const QcStructureDefinition &structure, const QcContentRegistry &content)
{
    QList<QcStructureDefinition::Loot> chests = structure.loot;
    for (const QcStructureDefinition::VersionChange &change : structure.generatorChanges) {
        chests.append(change.loot);
    }
    QStringList warnings;
    for (const QcStructureDefinition::Loot &loot : std::as_const(chests)) {
        if (!content.lootTables().find(loot.table)) {
            warnings.append(u"structure '%1': unknown loot table '%2', the chest stays empty"_s.arg(structure.name, loot.table));
        }
    }
    return warnings;
}

// The loot tables village styles swap, on both sides (decision E-1017).
QStringList checkStyleLoot(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcVillageStyleDefinition &style : content.villageCatalog().styles()) {
        const auto check = [&](const QString &table) {
            if (!content.lootTables().find(table)) {
                warnings.append(u"village style '%1': unknown loot table '%2' in 'loot'"_s.arg(style.name, table));
            }
        };
        for (const auto &[from, to] : style.loot.asKeyValueRange()) {
            check(from);
            check(to);
        }
    }
    return warnings;
}

// Whether a village style builds the structure `name`.
bool inAVillage(const QString &name, const QcContentRegistry &content)
{
    const auto builds = [&name](const QcVillagePiece &piece) {
        return std::ranges::any_of(piece.blueprints, [&name](const QcWeightedStructureName &blueprint) { return blueprint.structure == name; });
    };
    return std::ranges::any_of(content.villageCatalog().styles(), [&](const QcVillageStyleDefinition &style) {
        return style.centre == name || style.rareCentre == name || style.lamp == name || std::ranges::any_of(style.pieces, builds);
    });
}

// What `generatorChanges` of `structure` cannot do (decision E-1016): only villages and surface
// structures read it, and a version this game does not have is reached by no world.
QStringList checkGeneratorChanges(const QcStructureDefinition &structure, const QcContentRegistry &content)
{
    QStringList warnings;
    if (structure.generatorChanges.isEmpty()) {
        return warnings;
    }
    if (!structure.surface && !inAVillage(structure.name, content)) {
        warnings.append(u"structure '%1': only villages and surface structures read 'generatorChanges', it is built as written"_s.arg(structure.name));
    }
    for (const QcStructureDefinition::VersionChange &change : structure.generatorChanges) {
        if (change.version > QcGeneratorVersion::current) {
            warnings.append(u"structure '%1': 'generatorChanges' for version %2, this game generates up to %3"_s.arg(structure.name).arg(change.version).arg(QcGeneratorVersion::current));
        }
    }
    return warnings;
}

QStringList checkStructures(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcStructureDefinition &structure : content.worldgen().structures()) {
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
        // A spawn area (decision E-871) brings forth monsters only: kinds of the natural monster spawns.
        const QList<QcStructureDefinition::SpawnArea::Monster> areaMonsters = structure.spawnArea.value_or(QcStructureDefinition::SpawnArea{}).monsters;
        for (const QcStructureDefinition::SpawnArea::Monster &monster : areaMonsters) {
            const std::optional<QcCreatureKindId> type = content.mobKind(monster.mob);
            if (!type || !content.mobs()[*type].isMonster()) {
                warnings.append(u"structure '%1': spawn area with '%2', not a known monster, none come"_s.arg(structure.name, monster.mob));
            }
        }
        warnings.append(checkChests(structure, content));
        warnings.append(checkGeneratorChanges(structure, content));
    }
    warnings.append(checkStyleLoot(content));
    return warnings;
}

QStringList checkBlocks(const QcContentRegistry &content)
{
    QStringList warnings;
    QSet<QString> checked;
    for (qsizetype id = 0; id < content.stateCount(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
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
