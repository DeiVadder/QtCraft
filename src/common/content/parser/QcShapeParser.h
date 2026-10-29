// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcShapeDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// Parses block shapes: shape files and the inline shapes of blocks.
namespace QcShapeParser {

[[nodiscard]] QcResult<QcShapeDefinition> parseShape(const QJsonObject &json, const QString &packNamespace);
// Shape body shared by shape files and inline block shapes: "parts", or "boxes"/"crosses"/"collision"
// directly for a shape of one unconditional part.
[[nodiscard]] QcShapeDefinition parseShapeBody(const QJsonObject &json, QcJsonFieldReader &reader);
// The conditions at "when" ({"property": value or [values]}), as shape parts and block drops use them.
QList<QcShapeCondition> parseConditions(const QJsonObject &json, QcJsonFieldReader &reader);

} // namespace QcShapeParser
