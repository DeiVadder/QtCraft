// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/input/QcRelativeMouse.h"

struct QcRelativeMouse::Platform
{
};

QcRelativeMouse::QcRelativeMouse() = default;

QcRelativeMouse::~QcRelativeMouse() = default;

bool QcRelativeMouse::hasPlatformDeltas()
{
    return false;
}

void QcRelativeMouse::setCaptured(bool)
{
}

QPointF QcRelativeMouse::takeDelta()
{
    return {};
}
