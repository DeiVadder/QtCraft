// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcAdvancementDefinition.h"

#include <QString>

// A game event as the server's gameplay reports it to the adventure guide (decision E-923,
// QcServerAdvancements::trigger): the kind of event and the content names it involves (empty where
// the event has none). Inventory and location criteria are checked by the guide itself.
struct QcAdvancementEvent
{
    QcAdvancementTrigger trigger = QcAdvancementTrigger::Impossible;
    // The item crafted, smelted, used, bought or enchanted.
    QString item = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    QString block = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    // The creature kind (mob name).
    QString entity = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    // "minecart" or "boat".
    QString vehicle = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    QString effect = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    // The experience level reached.
    int level = 0;
};
