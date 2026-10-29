// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcBlastParser.h"

#include "common/content/parser/QcJsonFieldReader.h"

using namespace Qt::StringLiterals;

namespace QcBlastParser {

QcBlastDefinition parseBlast(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    QcBlastDefinition blast;
    blast.resistance = reader.number(u"blastResistance"_s, -1.0, 0.0, QcBlastDefinition::unbreakable);
    reader.nestedObject(u"explosive"_s, [&blast](const QJsonObject &, QcJsonFieldReader &nested) {
        blast.power = nested.number(u"power"_s, 4.0, 0.5, QcBlastDefinition::maxPower);
        blast.fuseTicks = nested.integer(u"fuse"_s, 80, 1, QcBlastDefinition::maxFuseTicks);
        blast.chainFuseMin = nested.integer(u"chainFuseMin"_s, 10, 1, QcBlastDefinition::maxFuseTicks);
        blast.chainFuseMax = nested.integer(u"chainFuseMax"_s, 30, 1, QcBlastDefinition::maxFuseTicks);
        blast.fire = nested.boolean(u"fire"_s, false);
        if (blast.chainFuseMax < blast.chainFuseMin) {
            nested.fail(u"'chainFuseMax' must not be below 'chainFuseMin'"_s);
        }
    });
    if (blast.isExplosive() && (block.liquid || !block.solid)) {
        reader.fail(u"an explosive must be a solid block"_s);
    }
    return blast;
}

} // namespace QcBlastParser
