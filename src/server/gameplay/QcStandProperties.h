// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/gameplay/QcArmourStandManager.h"

#include <QList>
#include <QString>
#include <QStringList>

class QcContentRegistry;

/*! \brief The properties of an armour stand that commands, structures and a later stand screen
    change (decision E-239), each checked here: "pose <part> <x> <y> <z>" (angles finite,
    normalised), "rotation <degrees>", "arms|plate|small|showname <true|false>", "name [text]"
    (cleaned, at most QcArmourStandManager::maxNameLength code points), "equip <set|item>"
    (armour into its slot, anything else into the hand), "cape <cape|none>" (an appearance of the
    kind cape), "clear" (takes everything off, the cape too) and "reset" (pose and flags). */
namespace QcStandProperties {

/*! Applies one property (\c {args[0]}, then its values in \a args) to \a stand, items read
    from \a content. Returns \c false (and the stand partly unchanged) when the property or a
    value is invalid. */
[[nodiscard]] bool apply(const QcContentRegistry &content, QcArmourStand &stand, const QStringList &args);
/*! Applies the property of a \a line of words ("pose head 10 0 0") to \a stand like apply(),
    with the items of \a content. Returns \c false when the property or a value is invalid. */
bool applyLine(const QcContentRegistry &content, QcArmourStand &stand, const QString &line);
/*! Returns the armour items of \a content of the set \a name (\c {qtcraft:iron}), else the one item
    of that name; empty when there is none. */
QList<QcItemStack> itemsNamed(const QcContentRegistry &content, const QString &name);
/*! Returns the item stands of structures drop: the first item of \a content (content order) that
    places a stand; empty when the content has none (such stands drop only what they wear and
    hold). */
QString standItem(const QcContentRegistry &content);

} // namespace QcStandProperties
