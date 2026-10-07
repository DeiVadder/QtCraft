#pragma once

#include <QString>

struct QcServerConfig
{
    QString worldName = QStringLiteral("World");
    qint64 seed = 0;
    // Radius in chunks streamed around each player.
    int viewDistance = 8;
    // Upper bound of chunks sent per client and tick, keeps ticks short while a client catches up.
    int chunksPerTick = 32;
};
