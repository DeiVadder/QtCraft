// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QList>

/*! \brief The status samples of the admin channel over time (decisions E-848, E-863).

    The server keeps one a second and one a minute (QcAdminStatusRecorder); a client that follows
    the status gets one a second and folds its minutes the same way, so the day's chart goes on
    without a new history. */
namespace QcAdminSamples {

/*! The samples of seconds kept: ten minutes. */
inline constexpr qsizetype secondsKept = 600;
/*! The samples of minutes kept: a day. */
inline constexpr qsizetype minutesKept = 1440;
/*! The seconds folded into one minute's sample. */
inline constexpr qsizetype secondsPerMinute = 60;

/*! Returns the seconds of one minute folded into one sample: means of the means and rates, the
    highest p99 and maximum, the latest counts and time. \a minute must not be empty. */
QcMsg::AdminSample folded(const QList<QcMsg::AdminSample> &minute);

/*! Returns \a history with the minute that ended folded into its minutes: when the seconds after
    the newest minute (all seconds without one) are a full minute. Unchanged otherwise. */
QcMsg::AdminHistory withMinuteFolded(QcMsg::AdminHistory history);

} // namespace QcAdminSamples
