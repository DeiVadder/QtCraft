// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcAppearanceDefinition.h"
#include "common/content/definitions/QcMobDefinition.h"
#include "common/content/definitions/QcModelDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses mobs and the skeleton models and appearances that characters are drawn with.
namespace QcMobParser {

[[nodiscard]] QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace);
[[nodiscard]] QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace);

} // namespace QcMobParser
