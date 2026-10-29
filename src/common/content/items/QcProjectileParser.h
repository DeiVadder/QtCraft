// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcProjectileDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses a kind of projectile (projectiles/<name>.json, decision E-425). All input is untrusted.
namespace QcProjectileParser {

[[nodiscard]] QcResult<QcProjectileDefinition> parseProjectile(const QJsonObject &json, const QString &packNamespace);

} // namespace QcProjectileParser
