// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QStringList>

/*! \brief Names the loaded content refers to that no pack provides, checked by QcModLoader after
    loading the mods: items, textures, sounds, blocks and creatures named by recipes, drops,
    creatures, appearances, blocks, carts and items, and the targets and subjects of world options.

    Such entries stay inert (a recipe never matches, a drop is not given, a sound is silent) but
    deserve a warning for the mod author. */
namespace QcContentReferences {

/*! Returns one warning per unknown name in \a content, in the order of the definitions. */
[[nodiscard]] QStringList check(const QcContentRegistry &content);

} // namespace QcContentReferences
