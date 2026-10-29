// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcEffectDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses a kind of status effect (effects/<name>.json, decision E-450). All input is untrusted.
namespace QcEffectParser {

[[nodiscard]] QcResult<QcEffectDefinition> parseEffect(const QJsonObject &json, const QString &packNamespace);

} // namespace QcEffectParser
