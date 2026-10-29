// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/recipes/QcFarmingParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"

using namespace Qt::StringLiterals;

namespace {

// The state property `name` of the block with at least two values, else a failure.
void requireStages(const QcBlockDefinition &block, const QString &name, QcJsonFieldReader &reader)
{
    const QcBlockProperty *property = block.property(name);
    if (reader.ok() && (!property || property->values.size() < 2)) {
        reader.fail(u"'property' must name a state of the block with at least two values"_s);
    }
}

} // namespace

namespace QcFarmingParser {

QcCropDefinition parseCrop(const QcBlockDefinition &block, QcJsonFieldReader &cropReader, const QString &packNamespace)
{
    QcCropDefinition crop;
    crop.property = cropReader.string(u"property"_s, u"age"_s);
    crop.averageSeconds = cropReader.number(u"averageSeconds"_s, 1200.0, 1.0, 86400.0);
    crop.dryFactor = cropReader.number(u"dryFactor"_s, 0.5, 0.0, 1.0);
    crop.minLight = cropReader.integer(u"minLight"_s, 9, 0, QcBlockDefinition::maxLightLevel);
    crop.fruit = cropReader.name(u"fruit"_s, packNamespace);
    crop.fruitSeconds = cropReader.number(u"fruitSeconds"_s, 300.0, 1.0, 86400.0);
    crop.fruitGround = QcContentFields::parseNames(u"fruitGround"_s, packNamespace, cropReader);
    crop.plantChance = cropReader.number(u"plantChance"_s, 0.0, 0.0, 1.0);

    requireStages(block, crop.property, cropReader);
    if (cropReader.ok() && block.support != QcBlockSupport::Below) {
        cropReader.fail(u"crops need 'support': \"below\""_s);
    }
    if (cropReader.ok() && block.solid) {
        cropReader.fail(u"crops must not be solid"_s);
    }
    if (cropReader.ok() && crop.bearsFruit() && crop.fruitGround.isEmpty()) {
        cropReader.fail(u"a 'fruit' needs 'fruitGround'"_s);
    }
    return crop;
}

QcSoilDefinition parseSoil(const QcBlockDefinition &block, QcJsonFieldReader &soilReader, const QString &packNamespace)
{
    QcSoilDefinition soil;
    soil.property = soilReader.string(u"property"_s, u"moist"_s);
    soil.water = soilReader.name(u"water"_s, packNamespace, u"qtcraft:water"_s);
    soil.reach = soilReader.integer(u"reach"_s, 4, 0, 8);
    soil.revertsTo = soilReader.name(u"revertsTo"_s, packNamespace);

    requireStages(block, soil.property, soilReader);
    if (soilReader.ok() && (soil.revertsTo.isEmpty() || soil.revertsTo == block.name)) {
        soilReader.fail(u"'revertsTo' must name another block"_s);
    }
    return soil;
}

} // namespace QcFarmingParser
