// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/handbook/QcHandbook.h"

#include <QMap>
#include <QString>
#include <QStringList>

// Builds the handbook from a complete content registry (base game and mods, including the server-side
// world-generation data and chapters): every block, item, recipe, creature, biome, liquid, world
// generation feature and tag gets an entry with facts and links. Labels come from the content's
// translations ("handbook.*" keys), so the result only depends on the registry, the language and
// the rule values.
namespace QcHandbookBuilder {

// `rules` are the values chapters refer to as {name} in their texts (e.g. {hunger.regenerationFood})
// and creature pages show as caps; the server provides them (QcHandbookRules). Unknown placeholders
// are listed in QcHandbook::problems(). `keys` fill key placeholders such as {key.cameraView} with
// key names (QcKeyBindings::placeholderKeys(): the defaults for the export, the player's keys in the
// game); "handbook.key.<name>" translates a key name.
QcHandbook build(const QcContentRegistry &content, const QString &language, const QMap<QString, double> &rules = {},
                 const QMap<QString, QStringList> &keys = {});

} // namespace QcHandbookBuilder
