// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

/*! \brief Reports an action the game mode refused (adventure mode, decision E-735) at most once
    in reportTicks ticks, so that a held button does not flood the HUD with notices.

    Part of QcClientSession, which emits QcClientSession::actionRefused() when report() agrees. */
class QcRefusalThrottle
{
public:
    /*! A refused action is reported at most once in this many ticks (2 s). */
    static constexpr quint64 reportTicks = 40;

    /*! An action was refused at tick \a tick. Returns whether to report it; a report holds back
        the next ones for reportTicks ticks. */
    [[nodiscard]] bool report(quint64 tick);

private:
    // The first tick at which the next refusal is reported.
    quint64 m_nextReport = 0;
};
