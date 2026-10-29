// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcVec3.h"

#include <QList>

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
    // Whether the client has every chunk of its view (a pause waits for it, decision E-877).
    bool viewComplete(const QcServerClient &client) const;

private:
    int radiusOf(const QcServerClient &client) const;
    // The chunks of the client's view it has not been sent.
    [[nodiscard]] QList<QcChunkPos> missingChunks(const QcServerClient &client) const;
    // The generator's labels of a chunk just sent (decision E-222).
    void sendLabels(QcServerClient &client, QcChunkPos pos) const;
    // The creatures spinning in the spawners of a chunk just sent (decision E-520) and the items on its
    // campfires (decision E-772).
    void sendSpawners(QcServerClient &client, QcChunkPos pos) const;

    QcServerState &m_state;
    QcChunkLoading &m_loading;
};
