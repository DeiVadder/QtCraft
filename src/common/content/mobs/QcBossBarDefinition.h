// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

/*! \brief The colour of a boss bar (decision E-1127), the seven colours of the role model's bars;
    the value is what the protocol sends (QcMsg::BossBar). */
enum class QcBossBarColour : quint8 {
    Pink, // \c {"pink"}
    Blue, // \c {"blue"}
    Red, // \c {"red"}
    Green, // \c {"green"}
    Yellow, // \c {"yellow"}
    Purple, // \c {"purple"}
    White, // \c {"white"}
};

/*! The number of boss bar colours (QcBossBarColour). */
inline constexpr int qcBossBarColours = 7;

/*! \brief A creature with a boss bar (decision E-1127), from its data (\c {bossBar}): every player
    in its dimension within \c {range} metres sees its name and health as a bar of \c {colour} at
    the top of the view. A showcase's display (without a mind or confined to its pen) shows none.
    Server side only; the bar itself goes to the clients as QcMsg::BossBar. \qcfields */
struct QcBossBarDefinition
{
    QcBossBarColour colour = QcBossBarColour::Purple;
    double range = 160.0;

    bool operator==(const QcBossBarDefinition &) const = default;
};
