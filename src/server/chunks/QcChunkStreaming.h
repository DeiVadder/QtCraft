#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcVec3.h"

class QcChunkLoading;
struct QcServerClient;
class QcServerState;

// Sends each client the chunks around it (nearest first, loading missing ones in the background
// through QcChunkLoading, which also unloads the chunks no one needs any more).
class QcChunkStreaming
{
public:
    QcChunkStreaming(QcServerState &state, QcChunkLoading &loading);

    static QcChunkPos chunkOf(const QcVec3 &position);

    void stream(QcServerClient &client);

private:
    // The generator's labels of a chunk just sent (decision E-222).
    void sendLabels(QcServerClient &client, QcChunkPos pos) const;
    // The creatures spinning in the spawners of a chunk just sent (decision E-520).
    void sendSpawners(QcServerClient &client, QcChunkPos pos) const;

    QcServerState &m_state;
    QcChunkLoading &m_loading;
};
