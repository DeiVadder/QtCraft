#include "clientcore/debug/QcDebugJson.h"

#include "common/core/QcIntMath.h"
#include "common/core/QcProcessMemory.h"

#include <QJsonArray>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Milliseconds with microsecond resolution, so the lines stay short.
double rounded(double ms)
{
    return std::round(ms * 1000.0) / 1000.0;
}

// Bytes as MiB with one decimal.
double megabytes(double bytes)
{
    return std::round(bytes / QcProcessMemory::bytesPerMb * 10.0) / 10.0;
}

QJsonObject timing(double meanMs, double maxMs)
{
    return {{u"mean"_s, rounded(meanMs)}, {u"max"_s, rounded(maxMs)}};
}

QJsonObject light(QcLightProbe::Levels levels)
{
    return {{u"sky"_s, levels.sky}, {u"block"_s, levels.block}};
}

QJsonObject targetJson(const QcDebugSnapshot::Target &target)
{
    QJsonObject state;
    for (const auto &[name, value] : target.state) {
        state.insert(name, value);
    }
    return {{u"position"_s, QJsonArray{target.pos.x, target.pos.y, target.pos.z}},
            {u"block"_s, target.block},
            {u"state"_s, state},
            {u"tags"_s, QJsonArray::fromStringList(target.tags)},
            {u"light"_s, light(target.light)}};
}

} // namespace

namespace QcDebugJson {

QJsonObject toJson(const QcDebugSnapshot &snapshot)
{
    QJsonObject json{{u"client"_s, clientJson(snapshot)}};
    if (snapshot.frame) {
        json.insert(u"frame"_s, frameJson(*snapshot.frame));
    }
    if (snapshot.server) {
        json.insert(u"server"_s, serverJson(*snapshot.server));
    }
    return json;
}

QJsonObject clientJson(const QcDebugSnapshot &snapshot)
{
    const QcBlockPos block = snapshot.block();
    const QcDebugSnapshot::Entities &entities = snapshot.entities;
    QJsonObject json{
        {u"tick"_s, static_cast<qint64>(snapshot.clientTick)},
        {u"protocol"_s, static_cast<qint64>(snapshot.protocol)},
        {u"world"_s, snapshot.worldName},
        {u"seed"_s, QString::number(snapshot.seed)},
        {u"position"_s, QJsonArray{rounded(snapshot.position.x), rounded(snapshot.position.y), rounded(snapshot.position.z)}},
        {u"block"_s, QJsonArray{block.x, block.y, block.z}},
        {u"chunk"_s, QJsonArray{block.chunk().x, block.chunk().z}},
        {u"section"_s, qcFloorDiv(block.y, QcWorld::sectionHeight)},
        {u"yaw"_s, rounded(snapshot.yaw)},
        {u"pitch"_s, rounded(snapshot.pitch)},
        {u"facing"_s, QString::fromLatin1(QcDebugSnapshot::facing(snapshot.yaw))},
        {u"light"_s, light(snapshot.feetLight)},
        {u"entities"_s, QJsonObject{{u"mobs"_s, entities.mobs},
                                    {u"players"_s, entities.players},
                                    {u"items"_s, entities.items},
                                    {u"fallingBlocks"_s, entities.fallingBlocks}}},
        {u"loadedChunks"_s, static_cast<qint64>(snapshot.loadedChunks)},
        {u"memoryMb"_s, std::round(snapshot.memoryMb)},
        {u"peakMemoryMb"_s, std::round(snapshot.peakMemoryMb)},
        {u"footprintMb"_s, std::round(snapshot.footprintMb)},
        {u"blocksMb"_s, megabytes(static_cast<double>(snapshot.blockBytes))},
    };
    if (snapshot.target) {
        json.insert(u"target"_s, targetJson(*snapshot.target));
    }
    if (!snapshot.targetEntity.isEmpty()) {
        json.insert(u"targetEntity"_s, snapshot.targetEntity);
    }
    return json;
}

QJsonObject frameJson(const QcDebugSnapshot::Frame &frame)
{
    QJsonObject phases;
    for (int phase = 0; phase < QcFramePhases::count; ++phase) {
        const QcDebugSnapshot::Timing &value = frame.phases[static_cast<std::size_t>(phase)];
        if (static_cast<QcFramePhase>(phase) != QcFramePhase::Gpu || frame.gpuTimed) {
            phases.insert(QLatin1StringView(QcFramePhases::ids[static_cast<std::size_t>(phase)]), timing(value.meanMs, value.maxMs));
        }
    }
    return {{u"fps"_s, std::round(frame.fps * 10.0) / 10.0},
            {u"phases"_s, phases},
            {u"drawCalls"_s, frame.drawCalls},
            {u"visibleSections"_s, frame.visibleSections},
            {u"meshedSections"_s, static_cast<qint64>(frame.meshedSections)},
            {u"meshBacklog"_s, static_cast<qint64>(frame.meshBacklog)},
            {u"lightJobsPending"_s, frame.lightJobsPending},
            {u"meshJobMs"_s, rounded(frame.meshJobMs)},
            {u"lightJobMs"_s, rounded(frame.lightJobMs)},
            {u"sectionMb"_s, megabytes(static_cast<double>(frame.sectionBytes))},
            {u"blockLightMb"_s, megabytes(static_cast<double>(frame.blockLightBytes))}};
}

QJsonObject serverJson(const QcMsg::ServerDebug &server)
{
    QJsonObject phases;
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        phases.insert(QLatin1StringView(QcTickPhases::ids[static_cast<std::size_t>(phase)]),
                      timing(server.phaseMeanMs[static_cast<std::size_t>(phase)], server.phaseMaxMs[static_cast<std::size_t>(phase)]));
    }
    QJsonObject json{{u"tick"_s, static_cast<qint64>(server.tick)},
            {u"phases"_s, phases},
            {u"tickMs"_s, timing(server.tickMeanMs, server.tickMaxMs)},
            {u"lateMs"_s, timing(server.lateMeanMs, server.lateMaxMs)},
            {u"skippedTicks"_s, static_cast<qint64>(server.skippedTicks)},
            {u"loadedChunks"_s, static_cast<qint64>(server.loadedChunks)},
            {u"players"_s, static_cast<qint64>(server.players)},
            {u"mobs"_s, static_cast<qint64>(server.mobs)},
            {u"items"_s, static_cast<qint64>(server.items)},
            {u"fallingBlocks"_s, static_cast<qint64>(server.fallingBlocks)},
            {u"pendingBlockUpdates"_s, static_cast<qint64>(server.pendingBlockUpdates)},
            {u"waitingBlockUpdates"_s, static_cast<qint64>(server.waitingBlockUpdates)},
            {u"biome"_s, server.biome},
            {u"worldType"_s, server.worldType},
            {u"chunks"_s, QJsonObject{{u"stored"_s, static_cast<qint64>(server.chunks.stored)},
                                       {u"arriving"_s, static_cast<qint64>(server.chunks.arriving)},
                                       {u"unsaved"_s, static_cast<qint64>(server.chunks.unsaved)},
                                       {u"groups"_s, static_cast<qint64>(server.chunks.groups)},
                                       {u"largestGroup"_s, static_cast<qint64>(server.chunks.largestGroup)},
                                       {u"playerGroup"_s, static_cast<qint64>(server.chunks.playerGroup)},
                                       {u"oversizeGroups"_s, static_cast<qint64>(server.chunks.oversizeGroups)},
                                       {u"tickets"_s, static_cast<qint64>(server.chunks.tickets)},
                                       {u"refusedTickets"_s, static_cast<qint64>(server.chunks.refusedTickets)},
                                       {u"forced"_s, static_cast<qint64>(server.chunks.forced)},
                                       {u"loaders"_s, static_cast<qint64>(server.chunks.loaders)}}}};

    if (!server.goals.isEmpty()) {
        static constexpr std::array<const char *, QcMsg::MobGoal::kindCount> kinds{"wander", "chase", "flee"};
        QJsonArray goals;
        for (const QcMsg::MobGoal &goal : server.goals) {
            goals.append(QJsonObject{{u"id"_s, static_cast<qint64>(goal.entityId)},
                                     {u"kind"_s, QString::fromLatin1(kinds[static_cast<std::size_t>(goal.kind)])},
                                     {u"goal"_s, QJsonArray{rounded(goal.goal.x), rounded(goal.goal.y), rounded(goal.goal.z)}}});
        }
        json.insert(u"goals"_s, goals);
    }
    return json;
}

} // namespace QcDebugJson
