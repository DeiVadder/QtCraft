#pragma once

#include "QcContentPackLoader.h"
#include "QcContentRegistry.h"
#include "QcResult.h"

#include <memory>

// Loads the base game content pack (embedded as :/content/qtcraft) into a new registry.
namespace QcBaseContent {

inline constexpr auto resourceDirectory = ":/content/qtcraft";

// Base pack only, with items for all blocks.
QcResult<std::shared_ptr<QcContentRegistry>> load(QStringList &warnings);
// Loads the base pack into `registry` without finishing it (mods follow, then createBlockItems()).
QcResult<QcContentPackInfo> loadInto(QcContentRegistry &registry, QStringList &warnings);

} // namespace QcBaseContent
