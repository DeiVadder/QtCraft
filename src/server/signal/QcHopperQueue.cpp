#include "server/signal/QcHopperQueue.h"

void QcHopperQueue::takeDue(quint64 now, QSet<QcBlockPos> &woken)
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

void QcHopperQueue::forgetChunk(QcChunkPos chunk)
{
    const auto inChunk = [chunk](QcBlockPos pos) { return pos.chunk() == chunk; };
    awake.removeIf(inChunk);
    touched.removeIf(inChunk);
    for (auto &[tick, positions] : timers) {
        positions.removeIf(inChunk);
    }
}

void QcHopperQueue::clear()
{
    awake.clear();
    timers.clear();
    touched.clear();
}
