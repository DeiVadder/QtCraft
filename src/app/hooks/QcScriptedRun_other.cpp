// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/hooks/QcScriptedRun.h"

QList<QRect> QcScriptedRun::builtInDisplays()
{
    return {};
}

void QcScriptedRun::keepOutOfForeground(int, char *[])
{
}

void QcScriptedRun::stayInBackground()
{
}

void QcScriptedRun::keepTimersRunning()
{
}
