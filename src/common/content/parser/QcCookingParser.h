#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// Parses the cooking fields of a block: "machine" (furnaces, smokers, blast furnaces; decision
// E-770), "campfire" (decision E-772) and "bites" (cake, decision E-773).
namespace QcCookingParser {

// The "machine" object `json`; its recipe category, speed, sound and kinds.
[[nodiscard]] QcMachineDefinition parseMachine(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);
// The "campfire" object `json`.
[[nodiscard]] QcCampfireDefinition parseCampfire(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);
// The "bites" object; `block` (read so far) needs the state `bites`.
[[nodiscard]] QcBitesDefinition parseBites(const QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcCookingParser
