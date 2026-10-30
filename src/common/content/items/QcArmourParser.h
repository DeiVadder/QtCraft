// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcArmourDefinition.h"

#include <QJsonObject>
#include <QString>

struct QcJsonFieldReader;

/*! \brief Parses the "armour" object of an item (decision E-235): slot, points, set, layer boxes
    and parts. */
namespace QcArmourParser {

/*! Returns the armour read from \a json. Failures go to \a reader; texture and set names get
    \a packNamespace, the set defaults to \a itemName. */
QcArmourDefinition parseArmour(const QJsonObject &json, const QString &packNamespace, const QString &itemName, QcJsonFieldReader &reader);

} // namespace QcArmourParser
