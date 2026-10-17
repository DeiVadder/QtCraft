#include "clientcore/debug/QcDebugText.h"

#include "common/core/QcIntMath.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QString ms(double value)
{
    return QString::number(value, 'f', value < 10.0 ? 2 : 1);
}

QString level(int value)
{
    return value < 0 ? u"?"_s : QString::number(value);
}

// One aligned row of the time column: name, mean and maximum.
QString row(const QString &name, int nameWidth, const QString &mean, const QString &max)
{
    return name.leftJustified(nameWidth) + mean.rightJustified(8) + max.rightJustified(8);
}

constexpr std::array<const char *, QcTickPhases::count> tickPhaseNames{
    QT_TRANSLATE_NOOP("QcDebugText", "Players"),        QT_TRANSLATE_NOOP("QcDebugText", "Saving"),
    QT_TRANSLATE_NOOP("QcDebugText", "Chunk loading"),  QT_TRANSLATE_NOOP("QcDebugText", "Block updates"),
    QT_TRANSLATE_NOOP("QcDebugText", "Signal Dust"),
    QT_TRANSLATE_NOOP("QcDebugText", "Liquids"),        QT_TRANSLATE_NOOP("QcDebugText", "Fire"),
    QT_TRANSLATE_NOOP("QcDebugText", "Weather"),        QT_TRANSLATE_NOOP("QcDebugText", "Creatures"),
    QT_TRANSLATE_NOOP("QcDebugText", "Projectiles"),
    QT_TRANSLATE_NOOP("QcDebugText", "Items"),          QT_TRANSLATE_NOOP("QcDebugText", "Plant growth"),
    QT_TRANSLATE_NOOP("QcDebugText", "Falling blocks"), QT_TRANSLATE_NOOP("QcDebugText", "Minecarts"),
    QT_TRANSLATE_NOOP("QcDebugText", "Machines"),
    QT_TRANSLATE_NOOP("QcDebugText", "Chunk streaming"), QT_TRANSLATE_NOOP("QcDebugText", "Client sync"),
    QT_TRANSLATE_NOOP("QcDebugText", "Messages"),
};
// Every phase has its name (an array initialised with fewer names ends in null pointers).
static_assert(std::ranges::none_of(tickPhaseNames, [](const char *name) { return name == nullptr; }));

constexpr std::array<const char *, QcFramePhases::count> framePhaseNames{
    QT_TRANSLATE_NOOP("QcDebugText", "Frame interval"), QT_TRANSLATE_NOOP("QcDebugText", "GUI thread"),
    QT_TRANSLATE_NOOP("QcDebugText", "Entity meshing"), QT_TRANSLATE_NOOP("QcDebugText", "Character preview"),
    QT_TRANSLATE_NOOP("QcDebugText", "Render thread"),
    QT_TRANSLATE_NOOP("QcDebugText", "World renderer"), QT_TRANSLATE_NOOP("QcDebugText", "Uploads"),
    QT_TRANSLATE_NOOP("QcDebugText", "GPU"),
};

constexpr std::array<const char *, 4> facingNames{
    QT_TRANSLATE_NOOP("QcDebugText", "north"),
    QT_TRANSLATE_NOOP("QcDebugText", "east"),
    QT_TRANSLATE_NOOP("QcDebugText", "south"),
    QT_TRANSLATE_NOOP("QcDebugText", "west"),
};

} // namespace

QString QcDebugText::phaseName(QcTickPhase phase)
{
    return tr(tickPhaseNames[std::size_t(phase)]);
}

QString QcDebugText::phaseName(QcFramePhase phase)
{
    return tr(framePhaseNames[std::size_t(phase)]);
}

QString QcDebugText::compact(const QcDebugSnapshot &snapshot)
{
    if (!snapshot.frame) {
        return tr("No frame statistics");
    }

    const QcDebugSnapshot::Timing &interval = snapshot.frame->phases[std::size_t(QcFramePhase::Interval)];
    return tr("%1 FPS · %2 ms (max %3 ms)").arg(qRound(snapshot.frame->fps)).arg(ms(interval.meanMs), ms(interval.maxMs));
}

QStringList QcDebugText::general(const QcDebugSnapshot &snapshot)
{
    QStringList lines{compact(snapshot)};
    lines.append(placeLines(snapshot));
    lines.append(targetLines(snapshot));
    lines.append(worldLines(snapshot));
    if (snapshot.server) {
        lines.append(serverLines(*snapshot.server));
    }
    return lines;
}

QStringList QcDebugText::placeLines(const QcDebugSnapshot &snapshot)
{
    const QcBlockPos block = snapshot.block();
    const QcChunkPos chunk = block.chunk();
    const QString worldType = snapshot.server ? snapshot.server->worldType : u"?"_s;
    QStringList lines{
        tr("Protocol %1 · world type %2 · seed %3").arg(snapshot.protocol).arg(worldType).arg(snapshot.seed),
        tr("XYZ: %1 / %2 / %3").arg(snapshot.position.x, 0, 'f', 3).arg(snapshot.position.y, 0, 'f', 3).arg(snapshot.position.z, 0, 'f', 3),
        tr("Block: %1 %2 %3 · chunk %4 %5 (at %6 %7) · section %8")
            .arg(block.x)
            .arg(block.y)
            .arg(block.z)
            .arg(chunk.x)
            .arg(chunk.z)
            .arg(block.localX())
            .arg(block.localZ())
            // Below the world (the void) the section counts on downwards.
            .arg(qcFloorDiv(block.y, QcWorld::sectionHeight)),
        tr("Facing: %1 (yaw %2°, pitch %3°)")
            .arg(tr(facingNames[std::size_t(QcDebugSnapshot::facingIndex(snapshot.yaw))]))
            .arg(snapshot.yaw, 0, 'f', 1)
            .arg(snapshot.pitch, 0, 'f', 1),
        tr("Light at the feet: sky %1, block %2").arg(level(snapshot.feetLight.sky), level(snapshot.feetLight.block)),
    };
    if (snapshot.server) {
        lines.append(tr("Biome: %1").arg(snapshot.server->biome.isEmpty() ? u"–"_s : snapshot.server->biome));
    }
    return lines;
}

QStringList QcDebugText::targetLines(const QcDebugSnapshot &snapshot)
{
    QStringList lines;
    if (const std::optional<QcDebugSnapshot::Target> &target = snapshot.target) {
        lines.append(tr("Target: %1 at %2 %3 %4").arg(target->block).arg(target->pos.x).arg(target->pos.y).arg(target->pos.z));
        if (!target->state.isEmpty()) {
            QStringList pairs;
            for (const auto &[name, value] : target->state) {
                pairs.append(name + u'=' + value);
            }
            lines.append(tr("  State: %1").arg(pairs.join(u", "_s)));
        }
        if (!target->tags.isEmpty()) {
            lines.append(tr("  Tags: %1").arg(target->tags.join(u", "_s)));
        }
        lines.append(tr("  Light in front: sky %1, block %2").arg(level(target->light.sky), level(target->light.block)));
    }
    if (!snapshot.targetEntity.isEmpty()) {
        lines.append(tr("Creature: %1").arg(snapshot.targetEntity));
    }
    return lines;
}

QStringList QcDebugText::worldLines(const QcDebugSnapshot &snapshot)
{
    const QcDebugSnapshot::Entities &entities = snapshot.entities;
    QStringList lines{
        tr("Entities: %1 creatures, %2 players, %3 items, %4 falling blocks")
            .arg(entities.mobs)
            .arg(entities.players)
            .arg(entities.items)
            .arg(entities.fallingBlocks),
    };

    if (const std::optional<QcDebugSnapshot::Frame> &frame = snapshot.frame) {
        lines.append(tr("Chunks: %1 loaded · sections: %2 meshed, %3 visible · %4 draw calls")
                         .arg(snapshot.loadedChunks)
                         .arg(frame->meshedSections)
                         .arg(frame->visibleSections)
                         .arg(frame->drawCalls));
        lines.append(tr("Meshing: %1 waiting (%2 ms each) · light jobs: %3 waiting (%4 ms each)")
                         .arg(frame->meshBacklog)
                         .arg(ms(frame->meshJobMs))
                         .arg(frame->lightJobsPending)
                         .arg(ms(frame->lightJobMs)));
    } else {
        lines.append(tr("Chunks: %1 loaded").arg(snapshot.loadedChunks));
    }

    lines.append(tr("Memory: %1 MB (peak %2 MB)").arg(qRound(snapshot.memoryMb)).arg(qRound(snapshot.peakMemoryMb)));
    return lines;
}

QStringList QcDebugText::serverLines(const QcMsg::ServerDebug &server)
{
    return {
        tr("Server: %1 chunks · %2 players · %3 creatures · %4 items · %5 falling blocks")
            .arg(server.loadedChunks)
            .arg(server.players)
            .arg(server.mobs)
            .arg(server.items)
            .arg(server.fallingBlocks),
        tr("Server tick %1: %2 ms (max %3 ms) · late %4 ms (max %5 ms) · %6 skipped")
            .arg(server.tick)
            .arg(ms(server.tickMeanMs), ms(server.tickMaxMs), ms(server.lateMeanMs), ms(server.lateMaxMs))
            .arg(server.skippedTicks),
        tr("Block updates: %1 queued, %2 waiting for chunks").arg(server.pendingBlockUpdates).arg(server.waitingBlockUpdates),
        tr("Chunks: %1 stored, %2 arriving, %3 not yet saved · forced %4 · loaders %5")
            .arg(server.chunks.stored)
            .arg(server.chunks.arriving)
            .arg(server.chunks.unsaved)
            .arg(server.chunks.forced)
            .arg(server.chunks.loaders),
        tr("Groups: %1 (largest %2, here %3, too large %4) · tickets %5 (refused %6)")
            .arg(server.chunks.groups)
            .arg(server.chunks.largestGroup)
            .arg(server.chunks.playerGroup)
            .arg(server.chunks.oversizeGroups)
            .arg(server.chunks.tickets)
            .arg(server.chunks.refusedTickets),
    };
}

QStringList QcDebugText::timings(const QcDebugSnapshot &snapshot)
{
    int width = 0;
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        width = std::max(width, int(phaseName(QcTickPhase(phase)).size()));
    }
    for (int phase = 0; phase < QcFramePhases::count; ++phase) {
        width = std::max(width, int(phaseName(QcFramePhase(phase)).size()));
    }
    width += 2;

    QStringList lines;
    if (const std::optional<QcDebugSnapshot::Frame> &frame = snapshot.frame) {
        lines.append(row(tr("Client frame"), width, tr("mean"), tr("max")));
        for (int phase = 0; phase < QcFramePhases::count; ++phase) {
            const QcDebugSnapshot::Timing &timing = frame->phases[std::size_t(phase)];
            const bool unknown = QcFramePhase(phase) == QcFramePhase::Gpu && !frame->gpuTimed;
            lines.append(row(phaseName(QcFramePhase(phase)), width, unknown ? u"–"_s : ms(timing.meanMs), unknown ? u"–"_s : ms(timing.maxMs)));
        }
    }

    if (const std::optional<QcMsg::ServerDebug> &server = snapshot.server) {
        lines.append(row(tr("Server tick"), width, tr("mean"), tr("max")));
        for (int phase = 0; phase < QcTickPhases::count; ++phase) {
            lines.append(row(phaseName(QcTickPhase(phase)), width, ms(server->phaseMeanMs[std::size_t(phase)]),
                             ms(server->phaseMaxMs[std::size_t(phase)])));
        }
        lines.append(row(tr("Whole tick"), width, ms(server->tickMeanMs), ms(server->tickMaxMs)));
    }
    return lines;
}
