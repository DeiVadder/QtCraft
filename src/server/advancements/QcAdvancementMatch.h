// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/advancements/QcAdvancementEvent.h"

class QcContentRegistry;
class QcInventory;

/*! \brief Whether an event or a player's state meets the conditions of a criterion (decision
    E-923).

    Pure, so each condition is testable on its own. */
namespace QcAdvancementMatch {

/*! Returns whether \a name, an item or block name of \a content, matches \a pattern, a content
    name or "#<tag>" of items. An empty pattern accepts everything. */
[[nodiscard]] bool matchesName(const QcContentRegistry &content, const QString &pattern, const QString &name);
/*! Returns whether \a event meets the \a conditions of an event trigger (everything but
    ItemObtained and Location), names read from \a content. */
[[nodiscard]] bool matches(const QcContentRegistry &content, const QcAdvancementConditions &conditions, const QcAdvancementEvent &event);
/*! Returns whether \a inventory meets the \a conditions of ItemObtained: it holds at least
    \c {count} items matching \c {item} (names read from \a content). */
[[nodiscard]] bool holds(const QcContentRegistry &content, const QcAdvancementConditions &conditions, const QcInventory &inventory);

/*! \brief Where a player stands, as far as the location criteria ask (each field filled only
    when needed). */
struct Place
{
    /*! The height of the feet's block. */
    int y = 0;
    /*! The biome there. */
    QString biome;
    /*! It lies in a village. */
    bool inVillage = false;
};
/*! Returns whether \a place meets the \a conditions of a location criterion. */
[[nodiscard]] bool matchesPlace(const QcAdvancementConditions &conditions, const Place &place);

} // namespace QcAdvancementMatch
