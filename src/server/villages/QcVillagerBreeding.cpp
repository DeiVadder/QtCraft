#include "QcVillagerBreeding.h"

#include "QcContentRegistry.h"
#include "QcDayCycle.h"
#include "QcLineOfSight.h"
#include "QcPhysics.h"
#include "QcVillages.h"

#include <algorithm>
#include <limits>

QcVillagerBreeding::QcVillagerBreeding(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

QList<QcVillagerBreeding::Birth> QcVillagerBreeding::check(QList<QcCreature> &mobs, const Context &context) const
{
    const QcVillagerBreedingSettings &settings = m_content->villagerSettings().breeding;
    const Villagers byVillage = villagersByVillage(mobs);
    qsizetype population = context.villages.population();
    // By id, so the villages that breed at the world's limit do not depend on the hash's order.
    QList<qint64> ids = byVillage.keys();
    std::ranges::sort(ids);
    QList<Birth> births;
    for (const qint64 id : std::as_const(ids)) {
        if (context.reconciled && !context.reconciled->contains(id)) {
            continue;
        }
        const QList<QcCreature *> &villagers = byVillage[id];
        share(villagers);
        const QcVillage *village = context.villages.find(id);
        if (!village || !breedsNow(*village, context, population)) {
            continue;
        }

        const std::optional<std::pair<QcCreature *, QcCreature *>> parents = pairIn(villagers, context);
        if (!parents) {
            continue;
        }
        auto [first, second] = *parents;
        const QcVec3 between = (first->motion.position + second->motion.position) * 0.5;
        const std::optional<QcBlockPos> bed = nearestFreeBed(*village, between);
        if (!bed) {
            continue;
        }
        for (QcCreature *parent : {first, second}) {
            parent->villager.produce -= settings.food;
            parent->breedCooldown = settings.cooldownSeconds;
        }
        births.append({village->id, *bed, first->motion.position});
        ++population;
    }
    return births;
}

bool QcVillagerBreeding::breedsNow(const QcVillage &village, const Context &context, qsizetype worldPopulation) const
{
    const QcVillagerActivity activity = m_content->villagerSettings().activityAt(QcDayCycle::phase(context.worldTime));
    const bool day = activity != QcVillagerActivity::Home && activity != QcVillagerActivity::Sleep;
    // A bed for the child beyond the villagers already living here: one whose bed was broken
    // claims the next free one.
    const bool bed = village.freeBeds() > 0 && village.population < village.beds.size();
    return day && !(context.rains && context.rains(village.centre)) && bed && village.population < context.limits.perVillage && worldPopulation < context.limits.world;
}

bool QcVillagerBreeding::willing(const QcCreature &mob) const
{
    const QcVillager &villager = mob.villager;
    return !mob.dormant && !mob.isYoung() && !villager.sleeping && villager.trader == 0 && mob.breedCooldown <= 0.0 && mob.fleeTimer <= 0.0
           && villager.produce >= m_content->villagerSettings().breeding.food;
}

QcVillagerBreeding::Villagers QcVillagerBreeding::villagersByVillage(QList<QcCreature> &mobs)
{
    Villagers byVillage;
    for (QcCreature &mob : mobs) {
        if (!mob.dormant && mob.villager.village != 0 && !mob.villager.profession.isEmpty()) {
            byVillage[mob.villager.village].append(&mob);
        }
    }
    return byVillage;
}

void QcVillagerBreeding::share(const QList<QcCreature *> &villagers) const
{
    const QcVillagerSettings &settings = m_content->villagerSettings();
    for (QcCreature *farmer : villagers) {
        const QcVillagerProfession *profession = settings.profession(farmer->villager.profession);
        if (!profession || !profession->tendsCrops || farmer->villager.produce <= settings.breeding.shareAbove) {
            continue;
        }

        QcCreature *hungry = nullptr;
        double nearest = settings.breeding.shareDistance;
        for (QcCreature *other : villagers) {
            const double distance = (other->motion.position - farmer->motion.position).length();
            if (other != farmer && !other->isYoung() && other->villager.produce < settings.breeding.food && distance <= nearest) {
                hungry = other;
                nearest = distance;
            }
        }
        if (hungry) {
            const int given = std::min(settings.breeding.food - hungry->villager.produce, farmer->villager.produce - settings.breeding.shareAbove);
            farmer->villager.produce -= given;
            hungry->villager.produce += given;
        }
    }
}

std::optional<std::pair<QcCreature *, QcCreature *>> QcVillagerBreeding::pairIn(const QList<QcCreature *> &villagers,
                                                                              const Context &context) const
{
    const QcVillagerBreedingSettings &settings = m_content->villagerSettings().breeding;
    for (qsizetype i = 0; i < villagers.size(); ++i) {
        QcCreature *first = villagers[i];
        if (!willing(*first)) {
            continue;
        }

        const QcMobDefinition &definition = m_content->mobs()[first->type];
        const QcVec3 eye = first->motion.position + QcVec3{0.0, definition.height * 0.85, 0.0};
        for (qsizetype j = i + 1; j < villagers.size(); ++j) {
            QcCreature *second = villagers[j];
            if (!willing(*second) || (second->motion.position - first->motion.position).length() > settings.partnerDistance) {
                continue;
            }
            // No children through walls.
            if (QcLineOfSight::reaches(context.world, eye, QcPhysics::boxAt(second->motion.position, definition.width, definition.height))) {
                return std::pair(first, second);
            }
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcVillagerBreeding::nearestFreeBed(const QcVillage &village, const QcVec3 &near)
{
    std::optional<QcBlockPos> nearest;
    double best = std::numeric_limits<double>::max();
    for (const QcVillage::Spot &bed : village.beds) {
        const double distance = (QcVec3{bed.at.x + 0.5, double(bed.at.y), bed.at.z + 0.5} - near).length();
        if (bed.claimedBy.isEmpty() && distance < best) {
            nearest = bed.at;
            best = distance;
        }
    }
    return nearest;
}
