#pragma once

#include "common/content/definitions/QcVillagerDefinitions.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses the villager settings (villagers.json) and the look of a village style's villagers
// (decision E-411). All input is untrusted.
namespace QcVillagerParser {

[[nodiscard]] QcResult<QcVillagerSettings> parseVillagerSettings(const QJsonObject &json, const QString &packNamespace);
// The look at `key` of the reader's object (outfit, hair and colour lists); unset without the key.
QcVillagerLook readLook(const QString &key, const QString &packNamespace, QcJsonFieldReader &reader);

} // namespace QcVillagerParser
