// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "server/commands/QcChunkRegeneration.h"
#include "server/commands/QcCommandRegistry.h"

#include <QString>

#include <optional>

class QcBlockUpdateQueue;
class QcPendingCommands;
class QcServerBlocks;
class QcServerState;

// Commands that change blocks (decision E-245): /setblock, /fill (optionally only replacing one
// block), /clear and /regenerate. Areas are limited in size; large fills and every regeneration
// wait for /confirm.
class QcBlockCommands
{
public:
    // Largest volume one /fill or /clear changes.
    static constexpr qint64 maxFillBlocks = 32768;
    // Fills larger than this ask for /confirm first.
    static constexpr qint64 confirmFillBlocks = 4096;

    QcBlockCommands(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates, QcPendingCommands &pending);

    void addTo(QcCommandRegistry &registry);

private:
    // Only blocks of this kind are replaced (all of its states unless the filter names states).
    struct Filter
    {
        std::optional<QcBlockId> block;
        bool explicitState = false;
    };

    struct BlockArea
    {
        QcBlockPos from;
        QcBlockPos low;
        QcBlockPos high;
        QString name;
        QcBlockId block = qcAirBlockId;
        // Without explicit states, connections follow the neighbours.
        bool explicitState = false;
        Filter filter;

        qint64 volume() const;
    };

    void setBlock(const QcCommandContext &context);
    void fill(const QcCommandContext &context, bool clear);
    void regenerate(const QcCommandContext &context);

    // The area of /fill and /clear (or the block of /setblock) from the words; replies on errors.
    [[nodiscard]] std::optional<BlockArea> parseArea(const QcCommandContext &context, qsizetype corners, const QString &blockWord) const;
    // The filter of /fill … replace and /clear (none given: no filter); none on errors, replied to.
    [[nodiscard]] std::optional<Filter> parseFilter(const QcCommandContext &context, bool clear) const;
    // The full name of a block word: "name" or "name[property=value,...]", in qtcraft: without a pack.
    [[nodiscard]] static QString blockName(const QString &text);
    // The block state of a block word; replies on errors.
    [[nodiscard]] std::optional<QcBlockId> parseBlock(const QcCommandContext &context, const QString &text) const;
    // Changes the blocks of the area and reports how many changed.
    void apply(const QcCommandSource &source, const BlockArea &area, bool single);
    // Sets one block of the area; false when its chunk is not loaded, the filter does not match or
    // no machine fits.
    bool changeBlock(QcBlockPos pos, const BlockArea &area, bool machine);

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcBlockUpdateQueue &m_updates;
    QcPendingCommands &m_pending;
    QcChunkRegeneration m_regeneration;
};
