// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcSoundDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses synthesized sound definitions of content packs. */
namespace QcSoundParser {

/*! Returns the sound described by \a json, with an unqualified name in \a packNamespace, or the
    first problem found (including any QcSoundDefinition::problem()). */
[[nodiscard]] QcResult<QcSoundDefinition> parseSound(const QJsonObject &json, const QString &packNamespace);

} // namespace QcSoundParser
