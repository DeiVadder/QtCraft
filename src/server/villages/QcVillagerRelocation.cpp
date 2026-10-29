// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/villages/QcVillagerRelocation.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "server/villages/QcVillages.h"

#include <cmath>
#include <limits>

QcVillagerRelocation::QcVillagerRelocation(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

bool QcVillagerRelocation::strayed(const QcVillage &village, const QcVec3 &at)
{
    const int beyond = QcVillage::reach + QcVillagerHomes::strayReach;
    const QcBlockPos cell = QcPhysics::blockContaining(at);
    return cell.x < village.minX - beyond || cell.x > village.maxX + beyond || cell.z < village.minZ - beyond || cell.z > village.maxZ + beyond;
}

int QcVillagerRelocation::check(QList<QcCreature> &mobs, const Context &context) const
{
    int moves = 0;
    for (QcCreature &mob : mobs) {
        // Villagers a structure keeps in place (a showcase farm) stay where they are.
        if (mob.dormant || !mob.isVillager() || mob.confined || mob.noAi || mob.pose == QcPose::Lying || mob.villager->trader != 0) {
            continue;
        }
        QcVillager &villager = *mob.villager;
        const QcVillagerSettings &settings = m_content->villagerSettingsOf(mob.type);
        const QcVillage *village = villager.village != 0 ? std::as_const(context.villages).find(villager.village) : nullptr;
        if (villager.village != 0 && (!village || (village->residents.known && !village->residents.uids.contains(mob.uid)))) {
            // Its settlement is gone, or it was away when its village was reconciled (its claims
            // lapsed, the population does not count it): homeless.
            QcVillagerHomes::moveOut(context.villages, villager, mob.uid, settings);
            village = nullptr;
        } else if (village && strayed(*village, mob.motion.position)) {
            QcVillagerHomes::moveOut(context.villages, villager, mob.uid, settings);
            village = nullptr;
            ++moves;
        }

        // Housed, or its own village has a bed for it (it claims it when it decides next).
        if (village && (villager.home || village->freeBeds() > 0)) {
            continue;
        }
        const QcVillage *found = newHome(context, mob.motion.position, villager.village);
        if (!found) {
            continue;
        }
        // Moving out may forget the old village, which moves others in the hash: by id.
        const qint64 home = found->id;
        if (village) {
            QcVillagerHomes::moveOut(context.villages, villager, mob.uid, settings);
        }
        QcVillagerHomes::moveIn(*context.villages.find(home), villager, mob.uid, mob.motion.position);
        villager.decideIn = 0;
        context.villages.noteChanged();
        ++moves;
    }
    return moves;
}

QcVillage *QcVillagerRelocation::newHome(const Context &context, const QcVec3 &at, qint64 except)
{
    const QcBlockPos cell = QcPhysics::blockContaining(at);
    const int chunks = (searchRadius + QcWorld::chunkSize - 1) / QcWorld::chunkSize;
    QcVillage *best = nullptr;
    double bestDistance = std::numeric_limits<double>::max();
    const QList<qint64> ids = context.villages.near(cell.chunk(), chunks);
    for (const qint64 id : ids) {
        QcVillage *village = context.villages.find(id);
        // A village it would stray from at once is no home (it would move out again).
        if (!village || id == except || village->freeBeds() == 0 || village->population >= village->beds.size()
            || village->population >= context.limits.perVillage || strayed(*village, at)) {
            continue;
        }
        for (const QcVillage::Spot &bed : std::as_const(village->beds)) {
            const double distance = std::hypot(bed.at.x + 0.5 - at.x, bed.at.z + 0.5 - at.z);
            if (bed.claimedBy == 0 && distance <= searchRadius && distance < bestDistance && context.settled(*village)) {
                best = village;
                bestDistance = distance;
            }
        }
    }
    return best;
}
