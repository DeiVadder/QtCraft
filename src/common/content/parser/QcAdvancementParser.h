#pragma once

#include "common/content/definitions/QcAdvancementDefinition.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

// Parses an advancement (advancements/<name>.json, decision E-923). All input is untrusted.
// `{"name": …, "disabled": true}` alone switches off an advancement of an earlier pack.
namespace QcAdvancementParser {

[[nodiscard]] QcResult<QcAdvancementDefinition> parseAdvancement(const QJsonObject &json, const QString &packNamespace);

// The trigger named `text` ("item_obtained", "block_broken", …); none for an unknown name.
[[nodiscard]] std::optional<QcAdvancementTrigger> triggerOf(const QString &text);
[[nodiscard]] QString triggerName(QcAdvancementTrigger trigger);
// "task", "goal", "challenge".
[[nodiscard]] QString frameName(QcAdvancementFrame frame);

} // namespace QcAdvancementParser
