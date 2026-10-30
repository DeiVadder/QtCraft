// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcProjectileDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses a kind of projectile (projectiles/<name>.json, decision E-425).

    All input is untrusted. */
namespace QcProjectileParser {

/*! Returns the projectile kind read from \a json, its names qualified with \a packNamespace,
    or the first failure. */
[[nodiscard]] QcResult<QcProjectileDefinition> parseProjectile(const QJsonObject &json, const QString &packNamespace);

} // namespace QcProjectileParser
