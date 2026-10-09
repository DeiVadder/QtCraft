#pragma once

#include "QcItemDefinition.h"
#include "QcResult.h"

#include <QJsonObject>

// Parses item definitions of content packs.
namespace QcItemParser {

QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace);

} // namespace QcItemParser
