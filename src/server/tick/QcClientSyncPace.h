// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

class QcServerClock;

// How often the clients hear of the world and of the time of day (decision E-249). Normally the
// world goes out with every tick and the time once per second of the players' time; during a sprint
// both go out at most once per tick interval of real time, not with every tick. Lives in the
// server's thread.
class QcClientSyncPace
{
public:
    // What the clients get after a tick besides their own state: the world around them (chunks,
    // entities, debug data) and the time of day.
    struct Due
    {
        bool world = true;
        bool time = false;
    };

    // What is due after the tick `clock` just counted; a sprint's sync is noted as done.
    [[nodiscard]] Due next(const QcServerClock &clock, bool sprinting);

private:
    // Players' time (QcServerClock::playerTimeNs) of the latest sync during a sprint.
    qint64 m_lastSprintSyncNs = 0;
};
