// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of the debug display and the runtime statistics (decision E-225). Part of the
// QcMessage variant (QcMessages.h).

#include "common/core/QcTickPhase.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QString>

#include <array>

/* QcMsg is documented in QcSessionMessages.h. */
namespace QcMsg {

/*! \brief Client -> server: which debug data the client wants while its debug display, the control
    interface or the statistics log needs it; 0 stops it.

    Limited like other spammable messages (QcRateLimiter::Kind::Debug). */
struct DebugSubscribe
{
    /*! ServerDebug messages with the tick phases and counts. */
    static constexpr quint8 stats = 1;
    /*! Their \c {goals}: where the creatures near the player are heading. */
    static constexpr quint8 mobGoals = 2;
    /*! Both. */
    static constexpr quint8 all = stats | mobGoals;

    /*! The data wanted, as the bits above. */
    quint8 flags = 0;
};

/*! \brief Where a creature is heading: chasing a player, wandering or fleeing. */
struct MobGoal
{
    /*! What the creature does. */
    enum class Kind : quint8 {
        /*! It wanders (or walks a path that is not a flight). */
        Wander,
        /*! It chases a player. */
        Chase,
        /*! It flees. */
        Flee
    };
    /*! The number of kinds. */
    static constexpr quint8 kindCount = 3;

    /*! The creature's entity id. */
    quint32 entityId = 0;
    /*! What it does. */
    Kind kind = Kind::Wander;
    /*! Where it is heading. */
    QcVec3 goal;

    bool operator==(const MobGoal &) const = default;
};

/*! \brief Server -> client: tick phases and server counts, at most twice a second while
    subscribed. */
struct ServerDebug
{
    /*! The most entries of \c {goals}. */
    static constexpr quint32 maxGoals = 256;
    /*! The most entries of \c {recentTickMs}. */
    static constexpr quint32 maxRecentTicks = 64;
    /*! The longest \c {biome} and \c {worldType}. */
    static constexpr qsizetype maxTextLength = 64;

    /*! Mean and maximum per phase (QcTickPhase) over the server's window of the latest ticks.
        \qcshared */
    std::array<float, QcTickPhases::count> phaseMeanMs{};
    std::array<float, QcTickPhases::count> phaseMaxMs{};
    /*! The whole tick (phases inside it) and its lateness against the 50 ms grid, same window:
        mean and maximum each. \qcshared */
    float tickMeanMs = 0.0f;
    float tickMaxMs = 0.0f;
    float lateMeanMs = 0.0f;
    float lateMaxMs = 0.0f;
    /*! Durations of the ticks since the previous message, oldest first (graph). */
    QList<float> recentTickMs;
    /*! The current tick of the world clock. */
    quint64 tick = 0;
    /*! Ticks dropped since the server started because it fell behind. */
    quint64 skippedTicks = 0;
    /*! Chunks loaded. */
    quint32 loadedChunks = 0;
    /*! Players who have joined. */
    quint32 players = 0;
    /*! Creatures that are not dormant, dropped items and falling blocks. \qcshared */
    quint32 mobs = 0;
    quint32 items = 0;
    quint32 fallingBlocks = 0;
    /*! Block updates of player actions and commands waiting for the next ticks. */
    quint32 pendingBlockUpdates = 0;
    /*! Block updates waiting for their chunk to load. */
    quint32 waitingBlockUpdates = 0;
    /*! \brief Chunks on the server (decisions E-374 to E-377): in its storage, under way (read
        or generated, waiting for their group), unloaded and not yet saved; groups of circuits
        among the loaded ones (more than one chunk), the largest, the one of the player's chunk,
        groups over the limit seen so far; load tickets of circuits (and those refused), forced
        chunks, chunk loaders. \qcfields */
    struct Chunks
    {
        quint32 stored = 0;
        quint32 arriving = 0;
        quint32 unsaved = 0;
        quint32 groups = 0;
        quint32 largestGroup = 0;
        quint32 playerGroup = 0;
        quint32 oversizeGroups = 0;
        quint32 tickets = 0;
        quint32 refusedTickets = 0;
        quint32 forced = 0;
        quint32 loaders = 0;

        bool operator==(const Chunks &) const = default;
    };
    /*! The chunk counts. */
    Chunks chunks;
    /*! Biome at the player's feet (registry name) and the world type (generator). \qcshared */
    QString biome;
    QString worldType;
    /*! Only with DebugSubscribe::mobGoals: creatures within goalRange of the player. */
    QList<MobGoal> goals;

    bool operator==(const ServerDebug &) const = default;
};

} // namespace QcMsg
