// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcShapeDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

struct QcJsonFieldReader;

/*! \brief Parses block shapes: shape files and the inline shapes of blocks. */
namespace QcShapeParser {

/*! Returns the shape file \a json with its \c {"name"} qualified in \a packNamespace, or the
    error. */
[[nodiscard]] QcResult<QcShapeDefinition> parseShape(const QJsonObject &json, const QString &packNamespace);
/*! Returns the shape body of \a json, shared by shape files and inline block shapes: "parts", or
    "boxes"/"crosses"/"collision" directly for a shape of one part, plus "rotate", "flip",
    "spin" and "lockTextures"; failures go to \a reader. */
[[nodiscard]] QcShapeDefinition parseShapeBody(const QJsonObject &json, QcJsonFieldReader &reader);
/*! Returns the conditions at "when" of \a json ({"property": value or [values]}), as shape parts
    and block drops use them; failures go to \a reader. */
QList<QcShapeCondition> parseConditions(const QJsonObject &json, QcJsonFieldReader &reader);

} // namespace QcShapeParser
