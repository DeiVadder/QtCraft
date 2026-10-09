#pragma once

#include "QcAppearanceDefinition.h"
#include "QcMobDefinition.h"
#include "QcModelDefinition.h"
#include "QcResult.h"

#include <QJsonObject>

// Parses mobs and the skeleton models and appearances that characters are drawn with.
namespace QcMobParser {

QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace);
QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace);
QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace);

} // namespace QcMobParser
