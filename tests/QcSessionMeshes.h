// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "common/core/QcSectionKey.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

#include <memory>

// The section meshes of one client session as the game keeps them (QcSessionWiring): the scheduler
// follows the session's chunks and blocks, the player's own blocks first (decision E-853), and is
// pumped by a timer like the frames are. Records per section how many meshes arrived, the vertices
// of the latest and when it arrived (nanoseconds on clock()).
class QcSessionMeshes : public QObject
{
    Q_OBJECT

public:
    // Takes the chunks the session has now and follows it from then on; `frameMs` is the time
    // between two pumps.
    QcSessionMeshes(QcClientSession &session, std::shared_ptr<const QcBlockRenderTable> table, int frameMs)
        : m_session(session)
    {
        m_clock.start();
        m_scheduler.setRenderTable(std::move(table));
        const QcClientWorld &world = *session.world();
        const QList<QcChunkPos> chunks = world.chunkPositions();
        for (const QcChunkPos pos : chunks) {
            m_scheduler.chunkLoaded(world, pos);
        }

        connect(&session, &QcClientSession::chunkLoaded, this, [this](QcChunkPos pos) { m_scheduler.chunkLoaded(*m_session.world(), pos); });
        connect(&session, &QcClientSession::chunkUnloaded, &m_scheduler, &QcMeshScheduler::chunkUnloaded);
        connect(&session, &QcClientSession::blockChanged, this,
                [this](QcBlockPos pos, QcBlockId previous) { m_scheduler.blockChanged(*m_session.world(), pos, previous); });
        connect(&session, &QcClientSession::playerBlockChanged, this, [this](QcBlockPos pos, QcBlockId previous) {
            m_scheduler.blockChanged(*m_session.world(), pos, previous, QcMeshScheduler::Priority::Player);
        });
        connect(&m_scheduler, &QcMeshScheduler::meshReady, this,
                [this](quint64 key, const std::shared_ptr<const QcChunkMesh> &mesh) { arrived(key, vertexCount(*mesh)); });
        connect(&m_scheduler, &QcMeshScheduler::meshRemoved, this, [this](quint64 key) { arrived(key, 0); });

        m_frames.setInterval(frameMs);
        m_frames.setTimerType(Qt::PreciseTimer);
        connect(&m_frames, &QTimer::timeout, this, [this] { m_scheduler.pump(*m_session.world()); });
        m_frames.start();
    }

    static quint64 sectionOf(QcBlockPos pos) { return qcSectionKey(pos.chunk(), pos.y / QcWorld::sectionHeight); }

    QcMeshScheduler &scheduler() { return m_scheduler; }
    const QElapsedTimer &clock() const { return m_clock; }
    int deliveries(quint64 key) const { return m_deliveries.value(key); }
    qsizetype vertices(quint64 key) const { return m_vertices.value(key); }
    qint64 deliveredAtNs(quint64 key) const { return m_deliveredAtNs.value(key, -1); }
    // Nothing waits to be meshed or lit.
    bool idle() const { return m_scheduler.dirtyCount() == 0 && m_scheduler.jobsInFlight() == 0 && m_scheduler.lightJobsPending() == 0; }

    // Waits for the running jobs and delivers their meshes now, without other events (no server
    // message is processed meanwhile).
    void deliverFinished()
    {
        m_scheduler.waitForJobs();
        QCoreApplication::sendPostedEvents(&m_scheduler);
    }

private:
    static qsizetype vertexCount(const QcChunkMesh &mesh)
    {
        return mesh.opaqueVertices.size() + mesh.cutoutVertices.size() + mesh.translucentVertices.size() + mesh.waterVertices.size();
    }

    void arrived(quint64 key, qsizetype vertices)
    {
        ++m_deliveries[key];
        m_vertices.insert(key, vertices);
        m_deliveredAtNs.insert(key, m_clock.nsecsElapsed());
    }

    QcClientSession &m_session;
    QcMeshScheduler m_scheduler;
    QTimer m_frames;
    QElapsedTimer m_clock;
    QHash<quint64, int> m_deliveries;
    QHash<quint64, qsizetype> m_vertices;
    QHash<quint64, qint64> m_deliveredAtNs;
};
