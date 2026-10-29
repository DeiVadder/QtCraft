// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/core/QcServerLimits.h"
#include "server/entities/QcHangingEntity.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <functional>
#include <optional>

// Item frames and paintings of a world (decision E-790): placed entities without AI, health or
// physics. Pure data; QcServerHanging does what players and the world do to them.
class QcHangingManager
{
public:
    // Frames and paintings in the world by default (QcServerLimits::maxHangings).
    static constexpr int maxHangings = static_cast<int>(QcServerLimits{}.maxHangings);
    // Every frame and painting checks its wall and room again within this many ticks, also without
    // a block change next to it (decision E-790; changes outside the server's block changes).
    static constexpr int supportCheckTicks = 100;

    const QList<QcHangingEntity> &all() const { return m_all; }
    // For waking and settling from outside (QcEntities); never add, remove or move through it (the
    // cell index would go stale).
    QList<QcHangingEntity> &editableAll() { return m_all; }
    QcHangingEntity *find(quint32 id);
    const QcHangingEntity *find(quint32 id) const;
    // The frame or painting that covers `cell` on `face`; null for none.
    const QcHangingEntity *at(QcBlockPos cell, QcBlockFace face) const;
    // False when the limit is reached.
    bool add(const QcHangingEntity &hanging);
    // Removes it and returns it (to drop what it carried).
    std::optional<QcHangingEntity> remove(quint32 id);
    qsizetype limit() const { return m_limit; }
    void setLimit(qsizetype limit) { m_limit = limit; }
    // Adds those of a chunk that came back (decision E-374); returns how many were left out at the limit.
    qsizetype adopt(const QList<QcHangingEntity> &hangings);
    // Removes those that match (their chunk leaves the world).
    void removeIf(const std::function<bool(const QcEntity &)> &matches);

    // A block changed at `pos`: the frames and paintings it holds or stands in check again
    // (decision E-790). One hash lookup, so a change far from any costs nothing.
    void noteBlockChange(QcBlockPos pos);
    // The ids to check this tick: those next to changed blocks and the next slice of the round in
    // which every one is checked once per supportCheckTicks ticks.
    QList<quint32> takeDueChecks();

private:
    // The cells and supporting blocks of `hanging`, the keys of the cell index.
    static QList<QcBlockPos> touchedBlocks(const QcHangingEntity &hanging);
    void index(const QcHangingEntity &hanging);
    void unindex(const QcHangingEntity &hanging);
    // Rebuilds where each id stands in m_all after entries left it.
    void renumber();

    QList<QcHangingEntity> m_all;
    qsizetype m_limit = maxHangings;
    // Where each id stands in m_all.
    QHash<quint32, qsizetype> m_position;
    // Ids by the cells they cover and the blocks that hold them.
    QHash<QcBlockPos, QList<quint32>> m_byBlock;
    QSet<quint32> m_changed;
    // Where the round of checks goes on in m_all.
    qsizetype m_roundNext = 0;
};
