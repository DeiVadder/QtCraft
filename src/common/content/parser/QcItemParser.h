// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses item definitions of content packs.
namespace QcItemParser {

[[nodiscard]] QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace);

} // namespace QcItemParser
