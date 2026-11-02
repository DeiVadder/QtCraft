// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/debug/QcDebugSnapshot.h"

#include <QJsonObject>

/*! \brief The debug data as JSON (decision E-225), the same numbers the debug display shows:
    "client" (position, light, target, entities, chunks, memory, latency), "frame" (graphical game
    only: FPS and the frame phases with mean and maximum in milliseconds, meshing) and "server"
    (tick phases, tick, lateness, counts; once the server sent them).

    Used by the control command \c {debug}, the statistics log (QTCRAFT_STATS) and documented in
    docs/Steuerschnittstelle.md. */
namespace QcDebugJson {

/*! Returns the whole \a snapshot: {"client", "frame" (when there is one), "server" (when there is
    one)}. */
QJsonObject toJson(const QcDebugSnapshot &snapshot);
/*! Returns the "client" part of \a snapshot. */
QJsonObject clientJson(const QcDebugSnapshot &snapshot);
/*! Returns the "frame" part: \a frame's rate, phases and meshing. */
QJsonObject frameJson(const QcDebugSnapshot::Frame &frame);
/*! Returns the "server" part: the phases, tick, lateness and counts of \a server. */
QJsonObject serverJson(const QcMsg::ServerDebug &server);
/*! Returns the round trip to the server, \a latency (decision E-886): {"measured":false} before
    the first answer, else
    {"measured":true,"smoothedMs","lastMs","minMs","maxMs","jitterMs","samples","pendingMs"} over
    the last 10 s; pendingMs is the age of a probe late beyond the smoothed value (a lower
    bound), else 0. */
QJsonObject latencyJson(const std::optional<QcRoundTripSummary> &latency);

} // namespace QcDebugJson
