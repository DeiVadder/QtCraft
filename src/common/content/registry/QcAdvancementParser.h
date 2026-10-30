// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcAdvancementDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

/*! \brief Parses an advancement (advancements/<name>.json, decision E-923).

    All input is untrusted. \c {{"name": …, "disabled": true}} alone switches off an advancement
    of an earlier pack. */
namespace QcAdvancementParser {

/*! Returns the advancement described by \a json, with unqualified names in \a packNamespace, or
    the first problem found. */
[[nodiscard]] QcResult<QcAdvancementDefinition> parseAdvancement(const QJsonObject &json, const QString &packNamespace);

/*! Returns the trigger named \a text ("item_obtained", "block_broken", …); none for an unknown
    name. */
[[nodiscard]] std::optional<QcAdvancementTrigger> triggerOf(const QString &text);
/*! Returns the JSON name of \a trigger, the inverse of triggerOf(). */
[[nodiscard]] QString triggerName(QcAdvancementTrigger trigger);
/*! Returns the JSON name of \a frame: "task", "goal" or "challenge". */
[[nodiscard]] QString frameName(QcAdvancementFrame frame);

} // namespace QcAdvancementParser
