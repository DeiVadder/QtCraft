#pragma once

#include "QcArmourStandManager.h"

#include <QList>
#include <QString>
#include <QStringList>

class QcContentRegistry;

// The properties of an armour stand that commands, structures and a later stand screen change
// (decision E-239), each checked here: "pose <part> <x> <y> <z>" (angles finite, normalised),
// "rotation <degrees>", "arms|plate|small|showname <true|false>", "name [text]" (cleaned, at most
// QcArmourStandManager::maxNameLength code points), "equip <set|item>" (armour into its slot,
// anything else into the hand), "cape <cape|none>" (an appearance of the kind cape), "clear" (takes
// everything off, the cape too) and "reset" (pose and flags).
namespace QcStandProperties {

// Applies one property (`args[0]`, then its values); false (and the stand partly unchanged) when
// the property or a value is invalid.
[[nodiscard]] bool apply(const QcContentRegistry &content, QcArmourStand &stand, const QStringList &args);
// The same for a line of words ("pose head 10 0 0").
bool applyLine(const QcContentRegistry &content, QcArmourStand &stand, const QString &line);
// The armour items of a set (`qtcraft:iron`), else the one item of that name; empty when there is none.
QList<QcItemStack> itemsNamed(const QcContentRegistry &content, const QString &name);
// The item stands of structures drop: the first item (content order) that places a stand; empty
// when the content has none (such stands drop only what they wear and hold).
QString standItem(const QcContentRegistry &content);

} // namespace QcStandProperties
