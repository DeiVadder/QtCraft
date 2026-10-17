#include "common/content/registry/QcVillageContent.h"

#include "common/content/registry/QcContentRegistry.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Block name -> workstation kind, from the professions in the order they were registered.
QHash<QString, QString> workstationsOf(const QcContentRegistry &registry, QStringList &warnings)
{
    QHash<QString, QString> workstations;
    for (const QcVillagerProfession &profession : registry.professions()) {
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
    return workstations;
}

// Whether `mob` names a creature kind that is a villager.
bool isVillager(const QcContentRegistry &registry, const QString &mob)
{
    const std::optional<int> type = registry.mobIndex(mob);
    return type && registry.mobs()[*type].villager;
}

// Gives every biome and the fallback to one style: the later registered one of those naming it.
void settleBiomes(QList<QcVillageStyleDefinition> &styles, QStringList &warnings)
{
    QHash<QString, qsizetype> owners;
    qsizetype fallback = -1;
    for (qsizetype index = 0; index < styles.size(); ++index) {
        for (const QString &biome : std::as_const(styles[index].biomes)) {
            const qsizetype other = owners.value(biome, -1);
            if (other < 0 || styles[other].registered < styles[index].registered) {
                owners.insert(biome, index);
            }
        }
        if (styles[index].fallback && (fallback < 0 || styles[fallback].registered < styles[index].registered)) {
            fallback = index;
        }
    }

    for (qsizetype index = 0; index < styles.size(); ++index) {
        QcVillageStyleDefinition &style = styles[index];
        const QStringList biomes = style.biomes;
        for (const QString &biome : biomes) {
            const qsizetype owner = owners.value(biome);
            if (owner != index) {
                warnings.append(u"village styles '%1' and '%2' both name biome '%3'; '%2', changed later, keeps it"_s.arg(
                    style.name, styles[owner].name, biome));
                style.biomes.removeAll(biome);
            }
        }
        if (style.fallback && index != fallback) {
            warnings.append(u"village styles '%1' and '%2' are both the fallback; '%2', changed later, stays it"_s.arg(style.name, styles[fallback].name));
            style.fallback = false;
        }
    }
}

// Warnings for the names of a style that the content lacks.
void checkStyleNames(const QcContentRegistry &registry, const QcVillageStyleDefinition &style, QStringList &warnings)
{
    for (const QString &biome : style.biomes) {
        if (!registry.biome(biome)) {
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
}

} // namespace

namespace QcVillageContent {

QStringList resolveSpecies(QcContentRegistry &registry)
{
    QStringList warnings;
    QcVillageSettings settings = registry.villageSettings();
    settings.workstations = workstationsOf(registry, warnings);
    registry.setVillageSettings(std::move(settings));

    QStringList broken;
    for (QcVillagerSettings &species : registry.villagerSpecies()) {
        species.professions = registry.professions();
        const QcVillagerProfession *unemployed = species.profession(species.unemployed);
        if (!unemployed || !unemployed->workstation.isEmpty()) {
            warnings.append(u"villager settings '%1': 'unemployed' must name a profession without workstations; left out"_s.arg(species.name));
            broken.append(species.name);
        }
    }
    for (const QString &name : std::as_const(broken)) {
        registry.removeVillagerSettings(name);
    }
    return warnings;
}

QStringList resolveStyles(QcContentRegistry &registry)
{
    QStringList warnings;
    settleBiomes(registry.villageStyles(), warnings);
    for (const QcVillageStyleDefinition &style : std::as_const(registry.villageStyles())) {
        checkStyleNames(registry, style, warnings);
    }
    const QString &villager = registry.villageSettings().villager;
    if (!villager.isEmpty() && !isVillager(registry, villager)) {
        warnings.append(u"village settings: 'villager' names no villager creature: '%1'"_s.arg(villager));
    }
    return warnings;
}

} // namespace QcVillageContent
