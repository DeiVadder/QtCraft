#include "QcMobBreeding.h"

#include "QcLineOfSight.h"
#include "QcPhysics.h"

#include <algorithm>
#include <cstdlib>

QcMobBreeding::QcMobBreeding(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

bool QcMobBreeding::wouldEat(const QcMobManager::Mob &mob, const QString &item) const
{
    if (mob.type < 0 || mob.type >= m_content->mobs().size()) {
        return false;
    }

    const QcBreedingDefinition &breeding = m_content->mobs()[mob.type].breeding;
    if (!breeding.breeds() || !breeding.food.contains(item)) {
        return false;
    }
    return mob.isYoung() || (mob.love <= 0.0 && mob.breedCooldown <= 0.0);
}

QcMobBreeding::Fed QcMobBreeding::feed(QcMobManager::Mob &mob, const QString &item) const
{
    if (!wouldEat(mob, item)) {
        return Fed::No;
    }

    const QcBreedingDefinition &breeding = m_content->mobs()[mob.type].breeding;
    if (mob.isYoung()) {
        mob.growUp = std::max(0.0, mob.growUp - breeding.growUpSeconds * feedGrowUpShare);
        return Fed::Young;
    }

    mob.love = breeding.loveSeconds;
    return Fed::Love;
}

QList<QcMobBreeding::Birth> QcMobBreeding::tick(const QcBlockAccess &world, QcMobManager &mobs, double dt) const
{
    QList<QcMobManager::Mob> &all = mobs.editableMobs();
    for (QcMobManager::Mob &mob : all) {
        if (!mob.dormant) {
            mob.love = std::max(0.0, mob.love - dt);
            mob.breedCooldown = std::max(0.0, mob.breedCooldown - dt);
            mob.growUp = std::max(0.0, mob.growUp - dt);
        }
    }

    approachPartners(all);

    QList<Birth> births;
    for (qsizetype i = 0; i < all.size(); ++i) {
        const qsizetype partner = courting(all[i]) ? partnerOf(world, all, i) : -1;
        if (partner < 0) {
            continue;
        }

        QcMobManager::Mob &first = all[i];
        QcMobManager::Mob &second = all[partner];
        const QcBreedingDefinition &breeding = m_content->mobs()[first.type].breeding;
        first.love = 0.0;
        second.love = 0.0;
        first.breedCooldown = breeding.cooldownSeconds;
        second.breedCooldown = breeding.cooldownSeconds;
        // At the feet of a parent: a place where an animal of its size stands.
        if (roomFor(all, first.type, first.motion.position)) {
            births.append({first.type, first.motion.position});
        }
    }
    return births;
}

void QcMobBreeding::approachPartners(QList<QcMobManager::Mob> &mobs) const
{
    // Animals in love walk towards the nearest other one of their kind in love (through the
    // wandering of QcMobManager, which keeps a direction until its timer runs out).
    for (QcMobManager::Mob &mob : mobs) {
        if (mob.dormant || mob.love <= 0.0 || mob.fleeTimer > 0.0) {
            continue;
        }

        const QcMobManager::Mob *nearest = nullptr;
        double best = m_content->mobs()[mob.type].breeding.partnerDistance * seekFactor;
        for (const QcMobManager::Mob &other : std::as_const(mobs)) {
            const double distance = (other.motion.position - mob.motion.position).length();
            if (&other != &mob && other.type == mob.type && other.love > 0.0 && !other.dormant && distance < best) {
                nearest = &other;
                best = distance;
            }
        }
        if (nearest) {
            const QcVec3 towards = nearest->motion.position - mob.motion.position;
            mob.wanderDirection = best > closeEnough ? QcVec3{towards.x, 0.0, towards.z}.normalized() : QcVec3{};
            mob.wanderTimer = 0.5;
        }
    }
}

bool QcMobBreeding::courting(const QcMobManager::Mob &mob) const
{
    const double loveSeconds = m_content->mobs()[mob.type].breeding.loveSeconds;
    return !mob.dormant && mob.love > 0.0 && mob.love <= loveSeconds - courtshipSeconds;
}

qsizetype QcMobBreeding::partnerOf(const QcBlockAccess &world, const QList<QcMobManager::Mob> &mobs, qsizetype index) const
{
    const QcMobManager::Mob &mob = mobs[index];
    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    const QcVec3 eye = mob.motion.position + QcVec3{0.0, definition.height * 0.85, 0.0};
    for (qsizetype other = index + 1; other < mobs.size(); ++other) {
        const QcMobManager::Mob &candidate = mobs[other];
        if (candidate.type != mob.type || !courting(candidate)
            || (candidate.motion.position - mob.motion.position).length() > definition.breeding.partnerDistance) {
            continue;
        }
        // No breeding through walls.
        if (QcLineOfSight::reaches(world, eye, QcPhysics::boxAt(candidate.motion.position, definition.width, definition.height))) {
            return other;
        }
    }
    return -1;
}

bool QcMobBreeding::roomFor(const QList<QcMobManager::Mob> &mobs, int type, const QcVec3 &position) const
{
    const QcChunkPos centre = QcPhysics::blockContaining(position).chunk();
    const auto near = [&](const QcMobManager::Mob &mob) {
        const QcChunkPos chunk = QcPhysics::blockContaining(mob.motion.position).chunk();
        return mob.type == type && std::abs(chunk.x - centre.x) <= 1 && std::abs(chunk.z - centre.z) <= 1;
    };
    const auto bred = [type](const QcMobManager::Mob &mob) { return mob.type == type && mob.bred; };
    return std::ranges::count_if(mobs, near) < crowdLimit && std::ranges::count_if(mobs, bred) < maxBredPerKind;
}
