// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcHoldDisplay.h"
#include "common/core/QcResult.h"

#include <QJsonObject>
#include <QString>

struct QcJsonFieldReader;

// Parses how items are held (decision E-831): the `hold` object of an item or block and the
// category defaults in `held.json`. A hold object has "category" (only on a definition), "grip"
// ("base", "handle", "middle") and "firstPerson"/"thirdPerson" objects with "translate" [x, y, z]
// in metres (each within ±maxTranslate), "rotate" [x, y, z] in degrees (±maxRotate) and "scale", a number
// or [x, y, z] (minScale..maxScale metres per unit); a "swing" object has "translate" and "rotate"
// like "firstPerson" (no "scale"). Values out of range or of the wrong type
// refuse the definition; unknown keys are only warned about (lcRegistry).
namespace QcHoldParser {

inline constexpr double maxTranslate = 2.0;
inline constexpr double maxRotate = 360.0;
inline constexpr double minScale = 0.01;
inline constexpr double maxScale = 2.0;

// The `hold` object of an item or block; failures go to `reader`.
QcHoldDisplay parseHold(const QJsonObject &json, QcJsonFieldReader &reader);
// The optional `hold` object of the item or block `reader` reads; an empty display without one.
QcHoldDisplay holdOf(QcJsonFieldReader &reader);
// `held.json`: {"categories": {"tool": {hold object without "category"}, ...}}.
[[nodiscard]] QcResult<QcHoldDefaults> parseHoldDefaults(const QJsonObject &json, const QString &packNamespace);

} // namespace QcHoldParser
