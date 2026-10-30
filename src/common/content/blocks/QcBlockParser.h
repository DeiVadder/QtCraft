// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses block definitions of content packs. */
namespace QcBlockParser {

/*! Returns the block definition read from \a json, names qualified in \a packNamespace, or the
    first error found. */
[[nodiscard]] QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace);

} // namespace QcBlockParser
