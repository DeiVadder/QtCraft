// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol message of the boss bars (decision E-1127). Part of the QcMessage variant
// (QcMessages.h).

#include <QString>
#include <QtGlobal>

namespace QcMsg {

/*! \brief Server -> client: the boss bar of the creature \c {entity} (decision E-1127): shown at
    the top of the screen while \c {shown}, with the creature kind's name \c {kind} (a mob name;
    the client shows its text) or the name a name tag gave it (\c {name}, empty for none), how
    full it is (\c {progress}, 0 to 1: its share of its health; a charging hydra heals) and
    its colour (QcBossBarColour). A message with \c {shown} false takes the bar away; the client
    forgets all bars on DimensionChange. \qcfields */
struct BossBar
{
    quint32 entity = 0;
    QString kind;
    QString name;
    float progress = 1.0f;
    quint8 colour = 0;
    bool shown = true;
};

} // namespace QcMsg
