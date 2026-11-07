// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcProgressionGoal.h"
#include "common/core/QcResult.h"

#include <QJsonObject>
#include <QStringList>

class QcContentRegistry;

/*! \brief Parses a progression goal (progression/<name>.json, decision E-1095).

    All input is untrusted. */
namespace QcProgressionGoalParser {

/*! Returns the goal read from \a json, its names qualified with \a packNamespace, or the first
    failure. */
[[nodiscard]] QcResult<QcProgressionGoal> parseProgressionGoal(const QJsonObject &json, const QString &packNamespace);
/*! Returns a warning for every goal of \a content that names an item, portal or dimension that
    does not exist (when the content is finished); such a goal stays and can never be reached. */
[[nodiscard]] QStringList checkTargets(const QcContentRegistry &content);

} // namespace QcProgressionGoalParser
