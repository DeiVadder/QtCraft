#pragma once

#include "common/content/definitions/QcMobDefinition.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonObject>

// Parses the traits of creatures that teleport, carry blocks and are provoked by a look (decision
// E-460): "gaze", "teleport", "carries" and "waterDamage"; and of companions (decision E-480):
// "tameable", "shy", "packRange", "frightens" and "scaredOfCats"; of water creatures and milking
// (decisions E-500, E-502): "aquatic" and "milk".
namespace QcMobTraitsParser {

void parseTraits(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace);

} // namespace QcMobTraitsParser
