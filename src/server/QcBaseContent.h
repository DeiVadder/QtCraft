#pragma once

#include "QcContentRegistry.h"
#include "QcResult.h"

#include <memory>

// Loads the base game content pack (embedded as :/content/qtcraft) into a new registry.
namespace QcBaseContent {

inline constexpr auto resourceDirectory = ":/content/qtcraft";

QcResult<std::shared_ptr<QcContentRegistry>> load(QStringList &warnings);

} // namespace QcBaseContent
