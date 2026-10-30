// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcEffectDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses a kind of status effect (effects/<name>.json, decision E-450).

    All input is untrusted. */
namespace QcEffectParser {

/*! Returns the effect read from \a json, its names qualified with \a packNamespace, or the
    first failure. */
[[nodiscard]] QcResult<QcEffectDefinition> parseEffect(const QJsonObject &json, const QString &packNamespace);

} // namespace QcEffectParser
