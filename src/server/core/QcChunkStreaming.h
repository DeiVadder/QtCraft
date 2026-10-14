#pragma once

#include "QcChunkPos.h"
#include "QcVec3.h"

struct QcServerClient;
class QcServerState;

// Sends each client the chunks around it (nearest first, generating missing ones in the background)
// and unloads chunks no player needs any more.
class QcChunkStreaming
{
public:
    explicit QcChunkStreaming(QcServerState &state);

    static QcChunkPos chunkOf(const QcVec3 &position);

    void stream(QcServerClient &client);
    // Keeps everything within streaming range of a player, sent or not yet sent.
    void unloadUnneeded();

private:
    // The generator's labels of a chunk just sent (decision E-222).
    void sendLabels(QcServerClient &client, QcChunkPos pos) const;

    QcServerState &m_state;
};
