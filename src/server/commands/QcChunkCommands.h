#pragma once

#include "QcCommandRegistry.h"

#include <optional>

class QcServerState;

// Chunks kept loaded on purpose (decision E-377): /forceload add|remove|query, by block coordinates
// (~ relative to the player) of one chunk or of the corners of an area. Forced chunks are kept in
// world.json and count against the limit forcedChunks; they load and stay like chunks around a
// player, with the groups of their circuits.
class QcChunkCommands
{
public:
    // Chunks one /forceload adds or removes at most.
    static constexpr int maxArea = 256;

    explicit QcChunkCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void forceload(const QcCommandContext &context);
    void add(const QcCommandContext &context, const QList<QcChunkPos> &chunks);
    void remove(const QcCommandContext &context, const QList<QcChunkPos> &chunks);
    void query(const QcCommandContext &context);
    // The chunks of the block columns from args[first] on: one column or two corners; nothing and a
    // reply when the words are no coordinates or the area is too large.
    [[nodiscard]] std::optional<QList<QcChunkPos>> area(const QcCommandContext &context, qsizetype first) const;

    QcServerState &m_state;
};
