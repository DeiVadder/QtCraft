// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcSwingSpeed.h"

#include <algorithm>

namespace {

// An effect's name without its namespace ("mypack:haste" -> "haste").
QStringView localName(const QString &name)
{
    const qsizetype colon = name.lastIndexOf(u':');
    return QStringView{name}.sliced(colon + 1);
}

} // namespace

int QcSwingSpeed::ticks() const
{
    // Wide arithmetic, so absurd levels clamp instead of overflowing.
    const long long length = static_cast<long long>(baseTicks) - std::max(faster, 0) + 2LL * std::max(slower, 0) + extraTicks;
    return static_cast<int>(std::clamp<long long>(length, minTicks, maxTicks));
}

QcSwingSpeed QcSwingSpeed::of(const QList<QcStatusEffect> &effects)
{
    QcSwingSpeed speed;
    for (const QcStatusEffect &effect : effects) {
        const QStringView name = localName(effect.name);
        if (name == hasteEffect) {
            speed.faster += effect.level;
        } else if (name == fatigueEffect) {
            speed.slower += effect.level;
        }
    }
    return speed;
}
