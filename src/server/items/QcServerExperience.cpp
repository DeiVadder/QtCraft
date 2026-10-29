#include "server/items/QcServerExperience.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"

#include <algorithm>
#include <cmath>

QcServerExperience::QcServerExperience(QcServerState &state, QcServerSounds &sounds)
    : m_state(state)
    , m_sounds(sounds)
{
}

void QcServerExperience::tick()
{
    QcExperienceOrbManager &orbs = m_state.entities().orbs();
    if (!orbs.orbs().isEmpty()) {
        QList<QcExperienceOrbManager::PlayerView> players;
        for (const auto &client : m_state.clients()) {
            // Spectators collect nothing (decision E-730).
            if (client->hasBody()) {
                players.append({client->id, client->motion.position, true});
            }
        }

        orbs.tick(m_state.world(), players, QcPhysics::tickDuration, [this](quint32 entityId, int points) { collected(entityId, points); });
    }

    sendChanges();
}

void QcServerExperience::collected(quint32 entityId, int points)
{
    for (const auto &client : m_state.clients()) {
        if (client->id == entityId) {
            m_sounds.play(QString::fromLatin1(QcSoundKeys::experienceOrb), client->motion.position, 0.5f);
            givePoints(*client, points);
        }
    }
}

void QcServerExperience::givePoints(QcServerClient &client, qint64 points)
{
    if (client.experience.addPoints(points) > 0) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::levelUp), client.motion.position, 0.75f);
    }
}

void QcServerExperience::sendChanges()
{
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || client->sentExperience == client->experience) {
            continue;
        }

        const QcExperience &experience = client->experience;
        // Every source of levels (orbs, commands, trades) ends here once per step (decision E-923).
        if (!client->sentExperience || client->sentExperience->level() < experience.level()) {
            m_state.advancements().trigger(*client, {.trigger = QcAdvancementTrigger::LevelReached, .level = experience.level()});
        }
        client->sentExperience = experience;
        client->connection->send(QcMsg::PlayerExperience{experience.level(), experience.points(), QcExperience::pointsForNext(experience.level())});
    }
}

void QcServerExperience::drop(QcServerState &state, int points, const QcVec3 &position)
{
    QcEntities &entities = state.entities();
    entities.orbs().drop(points, position, [&entities] { return entities.newId(); });
}

int QcServerExperience::takeWhole(QcServerState &state, double experience)
{
    if (!(experience > 0.0)) {
        return 0;
    }

    const double whole = std::floor(experience);
    return static_cast<int>(whole) + (state.random().generateDouble() < experience - whole ? 1 : 0);
}

int QcServerExperience::roll(QcServerState &state, QcExperienceDrop range)
{
    if (range.isEmpty()) {
        return 0;
    }

    const int low = std::clamp(range.min, 0, range.max);
    return low + static_cast<int>(state.random().bounded(static_cast<quint32>(range.max - low + 1)));
}
