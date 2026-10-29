// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/villages/QcTradeDefinitions.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses the offers of a profession (trades/<name>.json) and the trading settings of a villager
// species, villagers/<name>.json (decision E-414). All input is untrusted; item names are checked
// when the server builds its trade catalog (QcTradeCatalog), where all packs are loaded.
namespace QcTradeParser {

[[nodiscard]] QcResult<QcTradeSet> parseTradeSet(const QJsonObject &json, const QString &packNamespace);
// The settings at "trading" of the reader's object; the defaults without the key.
QcTradingSettings readTradingSettings(QcJsonFieldReader &reader);

} // namespace QcTradeParser
