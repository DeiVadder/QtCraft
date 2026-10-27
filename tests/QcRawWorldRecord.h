#pragma once

#include "common/protocol/QcMessages.h"

#include <QHash>
#include <QSet>

#include <functional>
#include <optional>

// What a raw protocol client (QcRawClient) recorded about the world: the entities with their spawns,
// moves and deaths, the chunks and their labels, the time of day, the weather and the debug data.
struct QcRawWorldRecord
{
    // Entities, labels and the world time.
    void receivedWorld(const QcMessage &message)
    {
        const bool aboutEntities = std::holds_alternative<QcMsg::EntitySpawn>(message) || std::holds_alternative<QcMsg::EntityMoves>(message)
                                   || std::holds_alternative<QcMsg::EntityRemove>(message);
        if (onEntityMessage && aboutEntities) {
            onEntityMessage(message);
        }
        if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
            entities.insert(spawn->id, spawn->kind);
            if (spawn->kind == QcEntityKind::Item) {
                items.insert(spawn->id, spawn->item);
            }
            spawns.insert(spawn->id, *spawn);
        } else if (const auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
            entities.remove(removed->id);
            items.remove(removed->id);
            if (removed->died) {
                died.insert(removed->id);
            }
        } else if (const auto *debug = std::get_if<QcMsg::ServerDebug>(&message)) {
            debugMessages.append(*debug);
        } else if (const auto *chunk = std::get_if<QcMsg::ChunkData>(&message)) {
            chunks.append(chunk->chunk->pos());
        } else if (const auto *labels = std::get_if<QcMsg::ChunkLabels>(&message)) {
            chunkLabels.insert(labels->pos, labels->labels);
        } else if (const auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
            ++timeMessages;
            worldTime = time->time;
            timeRunning = time->running;
            timeSpeed = time->speed;
        } else if (const auto *pause = std::get_if<QcMsg::ServerPause>(&message)) {
            pauses.append(pause->paused);
        } else if (const auto *weatherState = std::get_if<QcMsg::Weather>(&message)) {
            ++weatherMessages;
            weather = *weatherState;
        } else if (const auto *ack = std::get_if<QcMsg::BlockAck>(&message)) {
            blockAcks.append({ack->sequence, items.size()});
        } else if (const auto *strike = std::get_if<QcMsg::Lightning>(&message)) {
            lightning.append(strike->position);
        } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
            ++entityMoveMessages;
            for (const QcMsg::EntityMove &move : moves->moves) {
                flags[move.id] |= move.flags;
                held[move.id] = move.held;
                if (move.hurt) {
                    hurt.insert(move.id);
                }
            }
        }
    }

    // Called with every spawn, move and removal of an entity, in the order received (comparison runs).
    std::function<void(const QcMessage &)> onEntityMessage;
    // The WorldTime and EntityMoves messages received.
    int timeMessages = 0;
    int entityMoveMessages = 0;
    // Positions of the chunks received, in order.
    QList<QcChunkPos> chunks;
    // The kind of each entity the client knows.
    QHash<quint32, QcEntityKind> entities;
    // Item entities the client was told about, with their latest stack.
    QHash<quint32, QcItemStack> items;
    // Each BlockAck with the number of item entities known when it arrived (decision E-853).
    struct Ack
    {
        quint32 sequence = 0;
        qsizetype itemsKnown = 0;
    };
    QList<Ack> blockAcks;
    // Latest spawn message per entity, entities reported dead, and all move flags seen per entity.
    QHash<quint32, QcMsg::EntitySpawn> spawns;
    QSet<quint32> died;
    QHash<quint32, quint16> flags;
    // The latest held item per entity.
    QHash<quint32, QcItemId> held;
    // Entities reported hurt in any move.
    QSet<quint32> hurt;
    // Labels per chunk and the last world time.
    QHash<QcChunkPos, QList<QcWorldLabel>> chunkLabels;
    double worldTime = -1.0;
    // The latest weather, how many Weather messages came and where lightning struck (decision E-395).
    std::optional<QcMsg::Weather> weather;
    int weatherMessages = 0;
    QList<QcVec3> lightning;
    bool timeRunning = true;
    // Server ticks per client tick for the time of day (decision E-249).
    double timeSpeed = 1.0;
    // Every ServerPause received, in order (decision E-877).
    QList<bool> pauses;
    // Debug data received after a DebugSubscribe, oldest first.
    QList<QcMsg::ServerDebug> debugMessages;
};
