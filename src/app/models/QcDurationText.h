// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Durations as the screens show them, made in one place for the HUD and the "Manage server"
// screen: the time left of a status effect, the server's uptime.
namespace QcDurationText {

// "m:ss" (a status effect's time left, decision E-450); minutes are not capped at 59.
[[nodiscard]] QString minutesSeconds(int seconds);
// "2 d 5 h" from a day on, else "3 h 12 min" (translated, context "QcDurationText").
[[nodiscard]] QString daysHoursMinutes(qint64 ms);

} // namespace QcDurationText
