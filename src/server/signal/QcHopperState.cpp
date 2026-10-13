#include "QcHopperState.h"

void QcHopperState::startCooldown(QcBlockPos pos, quint64 now)
{
    readyAt.insert(pos, now + cooldownTicks);
    wakeAt(pos, now + cooldownTicks);
}

void QcHopperState::takeDue(quint64 now, QSet<QcBlockPos> &woken)
{
    woken.unite(awake);
    awake.clear();
    while (!timers.empty() && timers.begin()->first <= now) {
        for (const QcBlockPos &pos : std::as_const(timers.begin()->second)) {
            woken.insert(pos);
        }
        timers.erase(timers.begin());
    }
}

void QcHopperState::relocate(QcBlockPos from, QcBlockPos to)
{
    if (const auto it = readyAt.constFind(from); it != readyAt.constEnd()) {
        const quint64 tick = *it;
        readyAt.erase(it);
        readyAt.insert(to, tick);
    }
    awake.insert(to);
}

void QcHopperState::prune(quint64 now)
{
    readyAt.removeIf([now](const auto &it) { return it.value() <= now; });
}

void QcHopperState::forgetChunk(QcChunkPos chunk)
{
    const auto inChunk = [chunk](QcBlockPos pos) { return pos.chunk() == chunk; };
    awake.removeIf(inChunk);
    touched.removeIf(inChunk);
    readyAt.removeIf([&inChunk](const auto &it) { return inChunk(it.key()); });
    for (auto &[tick, positions] : timers) {
        positions.removeIf(inChunk);
    }
}

void QcHopperState::clear()
{
    awake.clear();
    timers.clear();
    readyAt.clear();
    touched.clear();
}
