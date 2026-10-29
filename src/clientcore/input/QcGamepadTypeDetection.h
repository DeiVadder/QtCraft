// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QString>
#include <QtGlobal>

// Which family a pad belongs to, from its USB/Bluetooth vendor and product id and its name
// (decision E-540): Microsoft is Xbox, Sony PlayStation, Nintendo's Pro Controller (and the
// combined Joy-Cons) Switch Pro, any other Nintendo product (a single Joy-Con) Generic; without a
// known vendor the name decides ("Xbox", "DualSense", "DualShock", "PS4", "PS5", "Pro Controller",
// "Switch"; a single "Joy-Con (L)"/"(R)" is Generic), else Generic. Backends that know the type
// themselves (SDL) use this only when they report none.
namespace QcGamepadTypeDetection {
QcGamepadType detect(quint16 vendor, quint16 product, const QString &name);
}
