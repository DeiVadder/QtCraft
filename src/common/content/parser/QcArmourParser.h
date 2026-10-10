#pragma once

#include "QcArmourDefinition.h"

#include <QJsonObject>
#include <QString>

struct QcJsonFieldReader;

// Parses the "armour" object of an item (decision E-235): slot, points, set, layer boxes and parts.
namespace QcArmourParser {

// Failures go to `reader`; texture names get `packNamespace`, the set defaults to `itemName`.
QcArmourDefinition parseArmour(const QJsonObject &json, const QString &packNamespace, const QString &itemName, QcJsonFieldReader &reader);

} // namespace QcArmourParser
