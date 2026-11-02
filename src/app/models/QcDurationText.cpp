// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcDurationText.h"

#include <QCoreApplication>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcDurationText {

QString minutesSeconds(int seconds)
{
    const int shown = std::max(0, seconds);
    return u"%1:%2"_s.arg(shown / 60).arg(shown % 60, 2, 10, QChar(u'0'));
}

QString daysHoursMinutes(qint64 ms)
{
    const qint64 minutes = std::max<qint64>(0, ms) / 60000;
    const qint64 days = minutes / 1440;
    const qint64 hours = minutes / 60 % 24;
    if (days > 0) {
        return QCoreApplication::translate("QcDurationText", "%1 d %2 h").arg(days).arg(hours);
    }
    return QCoreApplication::translate("QcDurationText", "%1 h %2 min").arg(hours).arg(minutes % 60);
}

} // namespace QcDurationText
