#pragma once

#include "QcJsonFieldReader.h"
#include "QcResult.h"
#include "QcTradeDefinitions.h"

#include <QJsonObject>

// Parses the offers of a profession (trades/<name>.json) and the trading settings of
// villagers.json (decision E-414). All input is untrusted; item names are checked when the server
// builds its trade catalog (QcTradeCatalog), where all packs are loaded.
namespace QcTradeParser {

[[nodiscard]] QcResult<QcTradeSet> parseTradeSet(const QJsonObject &json, const QString &packNamespace);
// The settings at "trading" of the reader's object; the defaults without the key.
QcTradingSettings readTradingSettings(QcJsonFieldReader &reader);

} // namespace QcTradeParser
