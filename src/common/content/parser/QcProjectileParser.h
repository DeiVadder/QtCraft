#pragma once

#include "common/content/definitions/QcProjectileDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses a kind of projectile (projectiles/<name>.json, decision E-425). All input is untrusted.
namespace QcProjectileParser {

[[nodiscard]] QcResult<QcProjectileDefinition> parseProjectile(const QJsonObject &json, const QString &packNamespace);

} // namespace QcProjectileParser
