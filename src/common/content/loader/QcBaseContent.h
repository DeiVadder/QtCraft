// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"

#include <QStringList>

#include <memory>

// Loads the base game content pack (embedded as :/content/qtcraft) into a new registry.
namespace QcBaseContent {

inline constexpr auto resourceDirectory = ":/content/qtcraft";

// The finished base content with the warnings of loading and finishing it.
struct Loaded
{
    std::shared_ptr<QcContentRegistry> registry;
    QStringList warnings;
};

// Base pack only, with items for all blocks.
[[nodiscard]] QcResult<Loaded> load();
// Loads the base pack into `registry` without finishing it (mods follow, then finish()).
[[nodiscard]] QcResult<QcLoadedPack> loadInto(QcContentRegistry &registry);

} // namespace QcBaseContent
