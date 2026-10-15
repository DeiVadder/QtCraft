#pragma once

#include "QcResult.h"
#include "QcSoundDefinition.h"

#include <QJsonObject>

// Parses synthesized sound definitions of content packs.
namespace QcSoundParser {

[[nodiscard]] QcResult<QcSoundDefinition> parseSound(const QJsonObject &json, const QString &packNamespace);

} // namespace QcSoundParser
