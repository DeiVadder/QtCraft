// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QList>

/*! \brief Whether the session shows the blocks of the local player's breaks and placements before
    the server confirms them, and the numbers of the predicted actions (decision E-853).

    Part of QcClientSession: it numbers each predicted action; the session shows the blocks in the
    world mirror and in the tick core. Numbers start anew with every session (a reconnect too). */
class QcBlockPrediction
{
public:
    /*! Returns whether a new session predicts blocks: on unless QTCRAFT_BLOCK_PREDICTION=0. */
    static bool wanted();

    /*! Returns whether the blocks are predicted. */
    bool isOn() const { return m_on; }
    /*! Turns the prediction on or off with \a on; applies from the next action on. */
    void setOn(bool on) { m_on = on; }
    /*! Returns the number of the latest predicted action, 0 before the first. */
    quint32 sequence() const { return m_sequence; }
    /*! Returns the number of a new action that sets \a blocks, or 0 when the prediction is off or
        \a blocks is empty (nothing to show). */
    [[nodiscard]] quint32 next(const QList<QcMsg::BlockUpdate> &blocks);

private:
    bool m_on = wanted();
    quint32 m_sequence = 0;
};
