// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcLightProbe.h"
#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/diagnostics/QcRoundTripStats.h"
#include "common/protocol/QcDebugMessages.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <utility>

class QcClientSession;

// Phases of a client frame for the debug display (decision E-225): the time between frames, CPU
// time of the GUI thread, its entity and hand meshing, the meshing of the character previews
// (decision E-231), CPU time of the render thread, the world renderer, its uploads and the GPU
// time of the window.
enum class QcFramePhase : quint8 { Interval, GuiCpu, EntityMesh, Preview, RenderCpu, WorldRender, Upload, Gpu };

namespace QcFramePhases {

inline constexpr int count = static_cast<int>(QcFramePhase::Gpu) + 1;
inline constexpr std::array<const char *, count> ids{"frame", "guiCpu", "entityMesh", "preview", "renderCpu", "worldRender", "upload", "gpu"};

} // namespace QcFramePhases

// Everything the debug display, the control interface and the statistics log show, at one moment
// (decision E-225). Plain data: collected from a session (and, in the graphical game, the frame
// statistics), then written as text lines (QcDebugText) or JSON (QcDebugJson).
struct QcDebugSnapshot
{
    struct Timing
    {
        double meanMs = 0.0;
        double maxMs = 0.0;
    };

    // Only in the graphical game.
    struct Frame
    {
        double fps = 0.0;
        // The window's frame rate limit in force (decision E-835); 0 for none.
        int frameLimit = 0;
        std::array<Timing, QcFramePhases::count> phases{};
        // GPU times need RHI timestamps (QSG_RHI_PROFILE=1).
        bool gpuTimed = false;
        int drawCalls = 0;
        int visibleSections = 0;
        qsizetype meshedSections = 0;
        // Sections waiting to be meshed or being meshed, and pending light jobs.
        qsizetype meshBacklog = 0;
        int lightJobsPending = 0;
        // Worker time per section mesh and per light job since the previous snapshot; 0 without jobs.
        double meshJobMs = 0.0;
        double lightJobMs = 0.0;
        // Bytes of the section buffers on the GPU and of the block light handed to the mesher
        // (decision E-813); the light only in a full snapshot (QcDebugHud::snapshot).
        quint64 sectionBytes = 0;
        qint64 blockLightBytes = 0;
    };

    struct Target
    {
        QcBlockPos pos;
        QcBlockFace face = QcBlockFace::Up;
        QString block;
        // Block state properties (name, value) and the tags of the block's item.
        QList<std::pair<QString, QString>> state;
        QStringList tags;
        // Light in the cell in front of the targeted face.
        QcLightProbe::Levels light;
    };

    struct Entities
    {
        int mobs = 0;
        int players = 0;
        int items = 0;
        int fallingBlocks = 0;
    };

    quint64 clientTick = 0;
    quint32 protocol = 0;
    QString worldName;
    qint64 seed = 0;
    QcVec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    QcLightProbe::Levels feetLight;
    std::optional<Target> target;
    // Name and id of the creature or player under the crosshair.
    QString targetEntity;
    Entities entities;
    qsizetype loadedChunks = 0;
    double memoryMb = 0.0;
    double peakMemoryMb = 0.0;
    double footprintMb = 0.0;
    // The blocks of the loaded chunks (QcClientWorld::memory, decision E-813).
    qint64 blockBytes = 0;
    std::optional<QcMsg::ServerDebug> server;
    std::optional<Frame> frame;
    // The round trip to the server (decision E-886); also in single player, where it is nearly 0.
    std::optional<QcRoundTripSummary> latency;

    // The block containing the feet, its chunk and section.
    QcBlockPos block() const;
    // The main direction of the yaw (0 looks north, -Z): 0 north, 1 east, 2 south, 3 west.
    static int facingIndex(float yaw);
    // "north", "east", "south" or "west".
    static const char *facing(float yaw);

    // The session's state now, without the frame statistics. `feet` is where the player is shown
    // (interpolated between ticks in the graphical game); without it, the session's position.
    static QcDebugSnapshot collect(const QcClientSession &session, const std::optional<QcVec3> &feet = std::nullopt);
};
