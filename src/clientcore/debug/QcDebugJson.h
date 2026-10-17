#pragma once

#include "clientcore/debug/QcDebugSnapshot.h"

#include <QJsonObject>

// The debug data as JSON (decision E-225), the same numbers the debug display shows: "client"
// (position, light, target, entities, chunks, memory), "frame" (graphical game only: FPS and the
// frame phases with mean and maximum in milliseconds, meshing) and "server" (tick phases, tick,
// lateness, counts; once the server sent them). Used by the control command `debug`, the
// statistics log (QTCRAFT_STATS) and documented in docs/Steuerschnittstelle.md.
namespace QcDebugJson {

QJsonObject toJson(const QcDebugSnapshot &snapshot);
QJsonObject clientJson(const QcDebugSnapshot &snapshot);
QJsonObject frameJson(const QcDebugSnapshot::Frame &frame);
QJsonObject serverJson(const QcMsg::ServerDebug &server);

} // namespace QcDebugJson
