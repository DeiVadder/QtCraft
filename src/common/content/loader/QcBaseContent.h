// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"

#include <QStringList>

#include <memory>

/*! \brief Loads the base game content pack (embedded as :/content/qtcraft) into a new registry. */
namespace QcBaseContent {

/*! The Qt resource directory holding the base pack. */
inline constexpr auto resourceDirectory = ":/content/qtcraft";

/*! \brief The finished base content with the warnings of loading and finishing it. */
struct Loaded
{
    /*! The registry holding the finished base content. */
    std::shared_ptr<QcContentRegistry> registry;
    /*! The warnings of loading the pack, then those of QcContentRegistry::finish(). */
    QStringList warnings;
};

/*! Returns the base pack alone in a new registry, finished (QcContentRegistry::finish(), which
    also creates the block items), with its warnings; an error when the pack cannot be loaded. */
[[nodiscard]] QcResult<Loaded> load();
/*! Loads the base pack into \a registry and adds its pack reference, without finishing it (mods
    follow, then finish()). Returns the loaded pack, or an error when it cannot be loaded. */
[[nodiscard]] QcResult<QcLoadedPack> loadInto(QcContentRegistry &registry);

} // namespace QcBaseContent
