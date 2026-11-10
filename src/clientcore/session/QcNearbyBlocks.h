// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientWorld.h"
#include "common/core/QcVec3.h"

#include <QList>

#include <functional>
#include <vector>

/*! \brief The blocks of one sort near the player (decision E-1022), for effects that follow from
    the blocks alone (campfire smoke, portal sparkles): found again every \c {rescanTicks} by
    looking only into the chunk sections whose palette holds such a block, the nearest at most
    \c {most}. */
class QcNearbyBlocks
{
public:
    /*! Returns whether a block state of the content is of the sort looked for. */
    using Sort = std::function<bool(const QcContentRegistry &content, QcBlockId id)>;
    /*! \brief How far and how often to look, and how many to keep. \qcfields */
    struct Reach
    {
        int radius = 32;
        int rescanTicks = 10;
        int most = 64;
    };

    /*! Looks for the blocks \a sort picks, as far and as often as \a reach says. */
    QcNearbyBlocks(Reach reach, Sort sort);

    /*! Looks for the blocks of \a world around \a eye again when \a tick is rescanTicks past the
        last look or the world's content changed. */
    void refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick);
    /*! Returns the blocks found by the latest look, the nearest first. */
    const QList<QcBlockPos> &found() const { return m_found; }

private:
    [[nodiscard]] QList<QcBlockPos> scan(const QcClientWorld &world, const QcVec3 &eye) const;
    // The blocks in one section whose first cell is at `origin`; none without one in its palette.
    [[nodiscard]] QList<QcBlockPos> inSection(const QcChunkSection &section, QcBlockPos origin) const;
    [[nodiscard]] bool picks(QcBlockId id) const;
    // Which block states are of the sort, per state id of the world's content.
    void learn(const QcContentRegistry &content);

    Reach m_reach;
    Sort m_sort;
    std::vector<bool> m_picks;
    const QcContentRegistry *m_content = nullptr;
    QList<QcBlockPos> m_found;
    quint64 m_scannedAt = 0;
    bool m_scanned = false;
};
