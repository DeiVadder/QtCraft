// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcAdvancementDefinition.h"

#include <QString>

/*! \brief A game event as the server's gameplay reports it to the adventure guide (decision E-923,
    QcServerAdvancements::trigger): the kind of event and the content names it involves (empty where
    the event has none).

    Inventory and location criteria are checked by the guide itself. */
struct QcAdvancementEvent
{
    /*! The kind of event. */
    QcAdvancementTrigger trigger = QcAdvancementTrigger::Impossible;
    /*! The item crafted, smelted, used, bought or enchanted. */
    QString item = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! The block broken, placed or used an item on. */
    QString block = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! The creature kind (mob name). */
    QString entity = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! "minecart" or "boat". */
    QString vehicle = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! The status effect received. */
    QString effect = {}; // NOLINT(readability-redundant-member-init): designated initializers may leave it out
    /*! The experience level reached, or the level of the beacon (decision E-1144). */
    int level = 0;
};
