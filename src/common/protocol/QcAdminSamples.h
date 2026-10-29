// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The status samples of the admin channel over time (decisions E-848, E-863): the server keeps one
// a second and one a minute (QcAdminStatusRecorder); a client that follows the status gets one a
// second and folds its minutes the same way, so the day's chart goes on without a new history.

#include "common/protocol/QcAdminMessages.h"

#include <QList>

namespace QcAdminSamples {

inline constexpr qsizetype secondsKept = 600;
inline constexpr qsizetype minutesKept = 1440;
inline constexpr qsizetype secondsPerMinute = 60;

// The seconds of one minute folded into one sample: means of the means and rates, the highest p99
// and maximum, the latest counts and time. `minute` must not be empty.
QcMsg::AdminSample folded(const QList<QcMsg::AdminSample> &minute);

// `history` with the minute that ended folded into its minutes: when the seconds after the newest
// minute (all seconds without one) are a full minute. Unchanged otherwise.
QcMsg::AdminHistory withMinuteFolded(QcMsg::AdminHistory history);

} // namespace QcAdminSamples
