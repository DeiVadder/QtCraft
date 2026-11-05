// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadMenuInput.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes QcGamepadMenuInput (qc_clientcore) known to QML as GamepadMenuInput, for its Command enum:
// a screen's gamepadCommand(command: int) compares with GamepadMenuInput.Command.Back and so on.
struct QcGamepadMenuInputForeign
{
    Q_GADGET
    QML_FOREIGN(QcGamepadMenuInput)
    QML_NAMED_ELEMENT(GamepadMenuInput)
    QML_UNCREATABLE("Only its Command enum is used in QML")
};
