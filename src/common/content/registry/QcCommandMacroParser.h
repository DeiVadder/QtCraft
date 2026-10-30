// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcCommandMacroDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses command macros (commands/\unicode {0x2A}.json, decision E-248). */
namespace QcCommandMacroParser {

/*! Returns the macro described by \a json, or the first problem found. \a packNamespace is not
    used: players type the bare name, and a later pack replaces an earlier macro of that name. */
[[nodiscard]] QcResult<QcCommandMacroDefinition> parseCommandMacro(const QJsonObject &json, const QString &packNamespace);

} // namespace QcCommandMacroParser
