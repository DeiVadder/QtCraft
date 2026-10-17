#pragma once

#include "common/content/definitions/QcSoundDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses synthesized sound definitions of content packs.
namespace QcSoundParser {

[[nodiscard]] QcResult<QcSoundDefinition> parseSound(const QJsonObject &json, const QString &packNamespace);

} // namespace QcSoundParser
