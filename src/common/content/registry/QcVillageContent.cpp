// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcVillageContent.h"

#include "common/content/registry/QcContentRegistry.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Block name -> workstation kind, from the professions in the order they were registered, and
// the warnings about blocks that are no workstation or the workstation of several professions.
struct Workstations
{
    QHash<QString, QString> kinds;
    QStringList warnings;
};

[[nodiscard]] Workstations workstationsOf(const QcContentRegistry &registry)
{
    Workstations result;
    QHash<QString, QString> &workstations = result.kinds;
    QStringList &warnings = result.warnings;
    for (const QcVillagerProfession &profession : registry.villageCatalog().professions()) {
        for (const QString &block : profession.workstationBlocks) {
            if (!registry.blockId(block)) {
                warnings.append(u"profession '%1': workstation '%2' is no block"_s.arg(profession.name, block));
                continue;
            }
            const QString earlier = workstations.value(block);
            if (!earlier.isEmpty() && earlier != profession.workstation) {
                warnings.append(u"block '%1' is the workstation of '%2' and '%3'; '%3' keeps it"_s.arg(block, earlier, profession.name));
            }
            workstations.insert(block, profession.workstation);
        }
    }
    return result;
}

// Whether `mob` names a creature kind that is a villager.
bool isVillager(const QcContentRegistry &registry, const QString &mob)
{
    const std::optional<int> type = registry.mobIndex(mob);
    return type && registry.mobs()[*type].villager;
}

// The style (by index) that owns each biome and the one that is the fallback (-1: none).
struct StyleOwners
{
    QHash<QString, qsizetype> biomes;
    qsizetype fallback = -1;
};

// Every biome and the fallback belong to one style: the later registered one of those naming it.
[[nodiscard]] StyleOwners ownersOf(const QList<QcVillageStyleDefinition> &styles)
{
    StyleOwners owners;
    for (qsizetype index = 0; index < styles.size(); ++index) {
        for (const QString &biome : styles[index].biomes) {
            const qsizetype other = owners.biomes.value(biome, -1);
            if (other < 0 || styles[other].registered < styles[index].registered) {
                owners.biomes.insert(biome, index);
            }
        }
        if (styles[index].fallback && (owners.fallback < 0 || styles[owners.fallback].registered < styles[index].registered)) {
            owners.fallback = index;
        }
    }
    return owners;
}

// Takes from the style at `index` the biomes and the fallback it does not own; a warning for every
// claim it lost.
[[nodiscard]] QStringList settleStyle(QcContentRegistry &registry, const StyleOwners &owners, qsizetype index)
{
    QStringList warnings;
    registry.villageCatalog().updateStyle(index, [&](QcVillageStyleDefinition &style) {
        const QList<QcVillageStyleDefinition> &styles = std::as_const(registry).villageCatalog().styles();
        const QStringList biomes = style.biomes;
        for (const QString &biome : biomes) {
            // A biome without an owner is no claim of two styles.
            const qsizetype owner = owners.biomes.value(biome, -1);
            if (owner >= 0 && owner != index) {
                warnings.append(u"village styles '%1' and '%2' both name biome '%3'; '%2', changed later, keeps it"_s.arg(
                    style.name, styles[owner].name, biome));
                style.biomes.removeAll(biome);
            }
        }
        if (style.fallback && index != owners.fallback) {
            warnings.append(u"village styles '%1' and '%2' are both the fallback; '%2', changed later, stays it"_s.arg(
                style.name, styles[owners.fallback].name));
            style.fallback = false;
        }
    });
    return warnings;
}

// Takes from each style of `registry` the biomes and the fallback it does not own.
[[nodiscard]] QStringList settleBiomes(QcContentRegistry &registry)
{
    const StyleOwners owners = ownersOf(registry.villageCatalog().styles());
    QStringList warnings;
    for (qsizetype index = 0; index < registry.villageCatalog().styles().size(); ++index) {
        warnings += settleStyle(registry, owners, index);
    }
    return warnings;
}

[[nodiscard]] QStringList checkStyleNames(const QcContentRegistry &registry, const QcVillageStyleDefinition &style)
{
    QStringList warnings;
    for (const QString &biome : style.biomes) {
        if (!registry.worldgen().biome(biome)) {
            warnings.append(u"village style '%1': unknown biome '%2'"_s.arg(style.name, biome));
        }
    }
    if (!style.villager.isEmpty() && !isVillager(registry, style.villager)) {
        warnings.append(u"village style '%1': 'villager' names no villager creature: '%2'"_s.arg(style.name, style.villager));
    }
    for (const QcVillageResident &resident : style.residents) {
        if (!registry.mobIndex(resident.mob)) {
            warnings.append(u"village style '%1': unknown resident '%2'"_s.arg(style.name, resident.mob));
        }
    }
    return warnings;
}

} // namespace

namespace QcVillageContent {

QStringList resolveSpecies(QcContentRegistry &registry)
{
    Workstations workstations = workstationsOf(registry);
    QStringList warnings = std::move(workstations.warnings);
    QcVillageSettings settings = registry.villageCatalog().settings();
    settings.workstations = std::move(workstations.kinds);
    registry.villageCatalog().setSettings(std::move(settings));

    QStringList broken;
    const QList<QcVillagerProfession> professions = registry.villageCatalog().professions();
    registry.villageCatalog().updateVillagerSpecies([&](QcVillagerSettings &species) {
        species.professions = professions;
        const QcVillagerProfession *unemployed = species.profession(species.unemployed);
        if (!unemployed || !unemployed->workstation.isEmpty()) {
            warnings.append(u"villager settings '%1': 'unemployed' must name a profession without workstations; left out"_s.arg(species.name));
            broken.append(species.name);
        }
    });
    for (const QString &name : std::as_const(broken)) {
        registry.villageCatalog().removeVillagerSettings(name);
    }
    return warnings;
}

QStringList resolveStyles(QcContentRegistry &registry)
{
    QStringList warnings = settleBiomes(registry);
    for (const QcVillageStyleDefinition &style : registry.villageCatalog().styles()) {
        warnings += checkStyleNames(registry, style);
    }
    const QString &villager = registry.villageCatalog().settings().villager;
    if (!villager.isEmpty() && !isVillager(registry, villager)) {
        warnings.append(u"village settings: 'villager' names no villager creature: '%1'"_s.arg(villager));
    }
    return warnings;
}

} // namespace QcVillageContent
