// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/villages/QcTradeDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses the offers of a profession (trades/<name>.json) and the trading settings of a
    villager species, villagers/<name>.json (decision E-414).

    All input is untrusted; item names are checked when the server builds its trade catalog
    (QcTradeCatalog), where all packs are loaded. */
namespace QcTradeParser {

/*! Returns the set of offers defined by \a json (a file in \c {trades}), names qualified with
    \a packNamespace, or the first error. */
[[nodiscard]] QcResult<QcTradeSet> parseTradeSet(const QJsonObject &json, const QString &packNamespace);
/*! Returns the settings at "trading" of the object of \a reader; the defaults without the key.
    Errors go to \a reader. */
QcTradingSettings readTradingSettings(QcJsonFieldReader &reader);

} // namespace QcTradeParser
