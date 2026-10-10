#pragma once

#include "QcDebugSnapshot.h"
#include "QcTickPhase.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

// The debug display's text (decision E-225), translated: one line for the compact level, the
// general column (position, light, target, entities, chunks, memory, server) and the time column
// (client frame and server tick phases, mean and maximum, aligned for a fixed-width font) for the
// full level. GUI-free.
class QcDebugText
{
    Q_DECLARE_TR_FUNCTIONS(QcDebugText)

public:
    // FPS and frame time.
    static QString compact(const QcDebugSnapshot &snapshot);
    static QStringList general(const QcDebugSnapshot &snapshot);
    static QStringList timings(const QcDebugSnapshot &snapshot);

    static QString phaseName(QcTickPhase phase);
    static QString phaseName(QcFramePhase phase);

private:
    static QStringList placeLines(const QcDebugSnapshot &snapshot);
    static QStringList targetLines(const QcDebugSnapshot &snapshot);
    static QStringList worldLines(const QcDebugSnapshot &snapshot);
    static QStringList serverLines(const QcMsg::ServerDebug &server);
};
