// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcPushBodies.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcBodyPush.h"

#include <algorithm>

using namespace Qt::StringLiterals;

void QcPushBodies::apply(const QcMessage &message)
{
    if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
        if (spawn->kind == QcEntityKind::Creature) {
            m_creatures.insert(spawn->id, {.type = spawn->creatureType, .feet = spawn->position, .step = {}, .flags = spawn->flags});
        }
    } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
        for (const QcMsg::EntityMove &move : moves->moves) {
            const auto it = m_creatures.find(move.id);
            if (it == m_creatures.end()) {
                continue;
            }
            // A step longer than maxLead in one tick is a jump (a teleport), no walk to go on with.
            const QcVec3 step{move.position.x - it->feet.x, 0.0, move.position.z - it->feet.z};
            it->step = step.length() <= maxLead ? step : QcVec3{};
            it->feet = move.position;
            it->flags = move.flags;
        }
    } else if (const auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
        m_creatures.remove(removed->id);
    }
}

QcVec3 QcPushBodies::ahead(const Creature &creature, int leadTicks)
{
    const QcVec3 lead = creature.step * std::clamp(leadTicks, 0, maxLeadTicks);
    const double length = lead.length();
    return creature.feet + (length > maxLead ? lead * (maxLead / length) : lead);
}

QcVec3 QcPushBodies::pushOnPlayer(const QcVec3 &feet, const QcContentRegistry &content, int leadTicks) const
{
    // A plain loop: the client knows the creatures of a few chunks around it only. It adds the
    // pushes in another order than the server's grid (QcBodyGrid), so with several creatures the
    // sums may differ in the last bits; far below a correction that matters.
    const QcBodyPush::Body self = QcBodyPush::player(feet);
    QcVec3 sum;
    for (const Creature &creature : m_creatures) {
        if (creature.type < 0 || creature.type >= content.mobs().size()) {
            continue;
        }
        const QcMobDefinition &definition = content.mobs()[creature.type];
        if (QcBodyPush::pushesPlayers(definition, QcMsg::poseOf(creature.flags))) {
            sum += QcBodyPush::between(self, QcBodyPush::creature(definition, ahead(creature, leadTicks)));
        }
    }
    return QcBodyPush::playerVelocity(sum);
}

bool QcPushBodies::extrapolationWanted()
{
    return qEnvironmentVariable("QTCRAFT_PUSH_EXTRAPOLATION") != u"0"_s;
}
