// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/definitions/QcItemDefinition.h"

using namespace Qt::StringLiterals;

QStringList QcItemDefinition::settleProjectileUses()
{
    QStringList warnings;
    if (throwable && (ignites || fertilizes || scoops || bottles || !emptiesTo.isEmpty())) {
        warnings.append(u"item '%1': a 'throwable' item neither ignites, fertilizes, scoops nor bottles; it is only thrown"_s.arg(name));
        ignites = false;
        fertilizes = false;
        scoops = false;
        bottles = false;
        emptiesTo.clear();
    }
    if (launcher && launcher->emptyAmmunitionList) {
        warnings.append(u"item '%1': an empty 'ammunition' list means none: the launcher takes all ammunition that is not thrown"_s.arg(name));
        launcher->emptyAmmunitionList = false;
    }
    return warnings;
}
