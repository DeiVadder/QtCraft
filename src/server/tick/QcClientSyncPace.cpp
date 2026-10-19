#include "server/tick/QcClientSyncPace.h"

#include "server/tick/QcServerClock.h"
#include "server/tick/QcTickScheduler.h"

QcClientSyncPace::Due QcClientSyncPace::next(const QcServerClock &clock, bool sprinting)
{
    // During a sprint the clients hear of the world at most once per interval of real time, not
    // with every tick (decision E-249); the time of day goes along then, at its sprint speed.
    const bool world = !sprinting || clock.playerTimeNs() - m_lastSprintSyncNs >= QcTickScheduler::intervalNs;
    if (sprinting && world) {
        m_lastSprintSyncNs = clock.playerTimeNs();
    }

    // Otherwise the time goes out once per second of the players' time.
    constexpr qint64 secondNs = 1'000'000'000;
    const bool second = clock.playerTimeNs() / secondNs != (clock.playerTimeNs() - clock.lastTickNs()) / secondNs;
    return {world, sprinting ? world : second};
}
