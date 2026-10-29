// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcArmourDefinition.h"

#include <QJsonObject>
#include <QString>

struct QcJsonFieldReader;

// Parses the "armour" object of an item (decision E-235): slot, points, set, layer boxes and parts.
namespace QcArmourParser {

// Failures go to `reader`; texture names get `packNamespace`, the set defaults to `itemName`.
QcArmourDefinition parseArmour(const QJsonObject &json, const QString &packNamespace, const QString &itemName, QcJsonFieldReader &reader);

} // namespace QcArmourParser
