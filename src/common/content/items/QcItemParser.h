// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses item definitions of content packs. */
namespace QcItemParser {

/*! Returns the item read from \a json, its names qualified with \a packNamespace, or the first
    failure. */
[[nodiscard]] QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace);

} // namespace QcItemParser
