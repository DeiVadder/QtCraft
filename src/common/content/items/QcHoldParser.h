// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcHoldDisplay.h"
#include "common/core/QcResult.h"

#include <QJsonObject>
#include <QString>

struct QcJsonFieldReader;

/*! \brief Parses how items are held (decision E-831): the \c {hold} object of an item or block and
    the category defaults in \c {held.json}.

    A hold object has "category" (only on a definition), "grip" ("base", "handle", "middle") and
    "firstPerson"/"thirdPerson" objects with "translate" [x, y, z] in metres (each within
    ±maxTranslate), "rotate" [x, y, z] in degrees (±maxRotate) and "scale", a number or [x, y, z]
    (minScale..maxScale metres per unit); a "swing" object has "translate" and "rotate" like
    "firstPerson" (no "scale"). Values out of range or of the wrong type refuse the definition;
    unknown keys are only warned about (lcRegistry). */
namespace QcHoldParser {

/*! Largest absolute value of a \c {translate} component, in metres. */
inline constexpr double maxTranslate = 2.0;
/*! Largest absolute value of a \c {rotate} component, in degrees. */
inline constexpr double maxRotate = 360.0;
/*! Smallest \c {scale} component, in metres per unit. */
inline constexpr double minScale = 0.01;
/*! Largest \c {scale} component, in metres per unit. */
inline constexpr double maxScale = 2.0;

/*! Returns the display read from \a json, the \c {hold} object of an item or block; failures go
    to \a reader. */
QcHoldDisplay parseHold(const QJsonObject &json, QcJsonFieldReader &reader);
/*! Returns the optional \c {hold} object of the item or block \a reader reads; an empty display
    without one. */
QcHoldDisplay holdOf(QcJsonFieldReader &reader);
/*! Returns the category defaults read from \a json, the content of \c {held.json}:
    {"categories": {"tool": {hold object without "category"}, ...}}, or the first failure. An
    unknown category is warned about and skipped. \a packNamespace is unused (category names are
    fixed); it keeps the signature of the other settings file parsers. */
[[nodiscard]] QcResult<QcHoldDefaults> parseHoldDefaults(const QJsonObject &json, const QString &packNamespace);

} // namespace QcHoldParser
