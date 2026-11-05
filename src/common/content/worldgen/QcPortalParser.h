// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcPortalDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses a portal (portals/<name>.json, decision E-1020).

    All input is untrusted. */
namespace QcPortalParser {

/*! Returns the portal read from \a json, its names qualified with \a packNamespace, or the first
    failure. */
[[nodiscard]] QcResult<QcPortalDefinition> parsePortal(const QJsonObject &json, const QString &packNamespace);

} // namespace QcPortalParser
