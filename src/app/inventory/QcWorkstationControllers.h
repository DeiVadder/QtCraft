// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/inventory/QcAnvilController.h"
#include "app/inventory/QcBeaconController.h"
#include "app/inventory/QcEnchantingController.h"
#include "app/inventory/QcSmithingController.h"
#include "app/inventory/QcStonecutterController.h"

#include <memory>

class QcSettingsController;
struct QcSessionParts;

// The controllers of the workstation screens with slots of their own: the stonecutter (decision
// E-785), the enchanting table (E-864), the anvil (E-866), the smithing table (E-1115) and the
// beacon (E-1142). The game
// controller owns them and shows them to QML; the session wiring hands them the session's signals.
struct QcWorkstationControllers
{
    QcWorkstationControllers(const QcSessionParts &parts, const QcSettingsController &settings)
        : stonecutter(std::make_unique<QcStonecutterController>(parts, settings))
        , enchanting(std::make_unique<QcEnchantingController>(parts, settings))
        , anvil(std::make_unique<QcAnvilController>(parts))
        , smithing(std::make_unique<QcSmithingController>(parts))
        , beacon(std::make_unique<QcBeaconController>(parts, settings))
    {
    }

    std::unique_ptr<QcStonecutterController> stonecutter;
    std::unique_ptr<QcEnchantingController> enchanting;
    std::unique_ptr<QcAnvilController> anvil;
    std::unique_ptr<QcSmithingController> smithing;
    std::unique_ptr<QcBeaconController> beacon;
};
