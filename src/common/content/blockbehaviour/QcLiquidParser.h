// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

/*! \brief Parses the "flow" object of a liquid block: how far and how fast it spreads, whether
    it makes new sources, its bucket, bottle and sound, and its reactions with other liquids. */
namespace QcLiquidParser {

/*! Returns the liquid of the "flow" object \a flowJson, names qualified in \a packNamespace;
    problems go to \a flowReader. */
[[nodiscard]] QcLiquidDefinition parseFlow(const QJsonObject &flowJson, QcJsonFieldReader &flowReader, const QString &packNamespace);
/*! Checks the states of a flowing liquid: the flow keeps its state in \c {level} ("0" source,
    then one value per block of distance, in order) and optionally \c {falling} (false, true);
    \a block (with its flow) must have them. Problems go to \a flowReader. */
void checkStates(const QcBlockDefinition &block, QcJsonFieldReader &flowReader);

} // namespace QcLiquidParser
