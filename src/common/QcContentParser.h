#pragma once

#include "QcBlockDefinition.h"
#include "QcResult.h"
#include "QcTextureDefinition.h"

#include <QHash>
#include <QJsonObject>

// Turns content-pack JSON documents into definitions. Unqualified names get the pack's namespace.
// All input is untrusted: every field is type-checked and range-checked.
namespace QcContentParser {

QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace);
QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace);
QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json);

} // namespace QcContentParser
