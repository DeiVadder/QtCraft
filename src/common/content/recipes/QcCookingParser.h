// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

/*! \brief Parses the cooking fields of a block: "machine" (furnaces, smokers, blast furnaces;
    decision E-770), "campfire" (decision E-772) and "bites" (cake, decision E-773). */
namespace QcCookingParser {

/*! Returns the "machine" object \a json read by \a reader: its recipe category, speed, sound
    (qualified with \a packNamespace, default the machine sound) and kinds. Failures go to
    \a reader. */
[[nodiscard]] QcMachineDefinition parseMachine(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);
/*! Returns the "campfire" object \a json read by \a reader, block names qualified with
    \a packNamespace; it needs "slots" or "lightsInto". Failures go to \a reader. */
[[nodiscard]] QcCampfireDefinition parseCampfire(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace);
/*! Returns the "bites" object read by \a reader; \a block (read so far) needs the state
    \c {bites} with at least two values. Failures go to \a reader. */
[[nodiscard]] QcBitesDefinition parseBites(const QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcCookingParser
