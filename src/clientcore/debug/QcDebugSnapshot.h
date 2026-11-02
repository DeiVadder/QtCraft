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

/*! \brief Phases of a client frame for the debug display (decision E-225): the time between
    frames, CPU time of the GUI thread, its entity and hand meshing, the meshing of the character
    previews (decision E-231), CPU time of the render thread, the world renderer, its uploads and
    the GPU time of the window. */
enum class QcFramePhase : quint8 {
    Interval,    // time between frames
    GuiCpu,      // CPU time of the GUI thread
    EntityMesh,  // its entity and hand meshing
    Preview,     // meshing of the character previews
    RenderCpu,   // CPU time of the render thread
    WorldRender, // the world renderer
    Upload,      // its uploads
    Gpu          // GPU time of the window
};

/*! \brief The number and the ids of the frame phases. */
namespace QcFramePhases {

/*! Number of frame phases. */
inline constexpr int count = static_cast<int>(QcFramePhase::Gpu) + 1;
/*! The id of each phase in JSON and the statistics log, in the order of QcFramePhase. */
inline constexpr std::array<const char *, count> ids{"frame", "guiCpu", "entityMesh", "preview", "renderCpu", "worldRender", "upload", "gpu"};

} // namespace QcFramePhases

/*! \brief Everything the debug display, the control interface and the statistics log show, at one
    moment (decision E-225).

    Plain data: collected from a session (and, in the graphical game, the frame statistics), then
    written as text lines (QcDebugText) or JSON (QcDebugJson). */
struct QcDebugSnapshot
{
    /*! \brief The mean and the maximum duration of a phase since the previous snapshot. */
    struct Timing
    {
        /*! Mean duration in milliseconds. */
        double meanMs = 0.0;
        /*! Longest duration in milliseconds. */
        double maxMs = 0.0;
    };

    /*! \brief Only in the graphical game. */
    struct Frame
    {
        /*! Frames per second. */
        double fps = 0.0;
        /*! The window's frame rate limit in force (decision E-835); 0 for none. */
        int frameLimit = 0;
        /*! The duration of each frame phase, indexed by QcFramePhase. */
        std::array<Timing, QcFramePhases::count> phases{};
        /*! GPU times need RHI timestamps (QSG_RHI_PROFILE=1). */
        bool gpuTimed = false;
        /*! Draw calls of the latest frame. */
        int drawCalls = 0;
        /*! Sections drawn in the latest frame. */
        int visibleSections = 0;
        /*! Sections that have a mesh. */
        qsizetype meshedSections = 0;
        /*! Sections waiting to be meshed or being meshed, and pending light jobs. \qcshared */
        qsizetype meshBacklog = 0;
        int lightJobsPending = 0;
        /*! Worker time per section mesh and per light job since the previous snapshot; 0 without
            jobs. \qcshared */
        double meshJobMs = 0.0;
        double lightJobMs = 0.0;
        /*! Bytes of the section buffers on the GPU and of the block light handed to the mesher
            (decision E-813); the light only in a full snapshot (QcDebugHud::snapshot). \qcshared */
        quint64 sectionBytes = 0;
        qint64 blockLightBytes = 0;
    };

    /*! \brief The block under the crosshair. */
    struct Target
    {
        /*! Its cell. */
        QcBlockPos pos;
        /*! The face aimed at. */
        QcBlockFace face = QcBlockFace::Up;
        /*! The block's name. */
        QString block;
        /*! Block state properties (name, value) and the tags of the block's item. */
        QList<std::pair<QString, QString>> state;
        /*! The tags of the block's item. */
        QStringList tags;
        /*! Light in the cell in front of the targeted face. */
        QcLightProbe::Levels light;
    };

    /*! \brief The entities of the mirror by kind. */
    struct Entities
    {
        /*! Creatures and the other kinds not counted below. */
        int mobs = 0;
        /*! Players. */
        int players = 0;
        /*! Dropped items. */
        int items = 0;
        /*! Falling blocks. */
        int fallingBlocks = 0;
    };

    /*! Client ticks since the start (QcClientSession::tickCount). */
    quint64 clientTick = 0;
    /*! The protocol version. */
    quint32 protocol = 0;
    /*! The world's name. */
    QString worldName;
    /*! The world's seed. */
    qint64 seed = 0;
    /*! Where the player's feet are. */
    QcVec3 position;
    /*! Where the player faces, in degrees. */
    float yaw = 0.0f;
    /*! The player's head pitch, in degrees. */
    float pitch = 0.0f;
    /*! Light in the cell of the feet. */
    QcLightProbe::Levels feetLight;
    /*! The block under the crosshair; nothing when none is targeted. */
    std::optional<Target> target;
    /*! Name and id of the creature or player under the crosshair. */
    QString targetEntity;
    /*! The entities by kind. */
    Entities entities;
    /*! Loaded chunks. */
    qsizetype loadedChunks = 0;
    /*! Resident memory of the process now, in MB. */
    double memoryMb = 0.0;
    /*! Highest resident memory of the process so far, in MB. */
    double peakMemoryMb = 0.0;
    /*! Physical footprint of the process now, in MB (macOS: with GPU memory). */
    double footprintMb = 0.0;
    /*! The blocks of the loaded chunks (QcClientWorld::memory, decision E-813). */
    qint64 blockBytes = 0;
    /*! The latest debug data of the server; nothing without a subscription. */
    std::optional<QcMsg::ServerDebug> server;
    /*! The frame statistics; only in the graphical game. */
    std::optional<Frame> frame;
    /*! The round trip to the server (decision E-886); also in single player, where it is nearly
        0. */
    std::optional<QcRoundTripSummary> latency;

    /*! Returns the block containing the feet, its chunk and section. */
    QcBlockPos block() const;
    /*! Returns the main direction of \a yaw (0 looks north, -Z): 0 north, 1 east, 2 south, 3
        west. */
    static int facingIndex(float yaw);
    /*! Returns the main direction of \a yaw: "north", "east", "south" or "west". */
    static const char *facing(float yaw);

    /*! Returns the state of \a session now, without the frame statistics. \a feet is where the
        player is shown (interpolated between ticks in the graphical game); without it, the
        session's position. */
    static QcDebugSnapshot collect(const QcClientSession &session, const std::optional<QcVec3> &feet = std::nullopt);
};
