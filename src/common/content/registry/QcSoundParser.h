// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcSoundDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses synthesized sound definitions of content packs.
namespace QcSoundParser {

[[nodiscard]] QcResult<QcSoundDefinition> parseSound(const QJsonObject &json, const QString &packNamespace);

} // namespace QcSoundParser
