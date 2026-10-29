// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobs/QcMobLaying.h"

QList<QcMobLaying::Laid> QcMobLaying::tick(const QcContentRegistry &content, QList<QcCreature> &mobs, QRandomGenerator &random, double dt)
{
    QList<Laid> laid;
    for (QcCreature &mob : mobs) {
        if (mob.dormant || mob.isYoung() || mob.type < 0 || mob.type >= content.mobs().size()) {
            continue;
        }

        const QcLayingDefinition &laying = content.mobs()[mob.type].laying;
        if (!laying.lays()) {
            continue;
        }
        if (mob.layTimer < 0.0) {
            mob.layTimer = nextTime(laying, random);
            continue;
        }

        mob.layTimer -= dt;
        if (mob.layTimer <= 0.0) {
            laid.append({laying.item, laying.sound, mob.motion.position});
            mob.layTimer = nextTime(laying, random);
        }
    }
    return laid;
}

double QcMobLaying::nextTime(const QcLayingDefinition &laying, QRandomGenerator &random)
{
    return laying.minSeconds + random.generateDouble() * (laying.maxSeconds - laying.minSeconds);
}
