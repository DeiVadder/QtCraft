// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/tick/QcTimerResolution.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#endif

bool QcTimerResolution::raise()
{
#ifdef Q_OS_WIN
    return timeBeginPeriod(1) == TIMERR_NOERROR;
#else
    return false;
#endif
}

QcTimerResolution::~QcTimerResolution()
{
#ifdef Q_OS_WIN
    if (m_raised) {
        timeEndPeriod(1);
    }
#endif
}
