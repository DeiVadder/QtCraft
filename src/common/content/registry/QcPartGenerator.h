// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QcContentRegistry;

// The registry function behind building parts: for every material of the registry and every part
// family of the material's kind, it registers the part's block (item follows from createBlockItems),
// its crafting recipe and its display names. Base game and mods use it alike, through materials/
// and parts/ files (decision E-048).
//
// Nothing is made for excluded materials: base blocks listed by an exclusion entry, and base blocks
// that are no plain building block (not an opaque solid full cube, liquid, with states, a machine,
// crafting table or storage, unbreakable, glowing or a sapling). A material leaves out the families
// in its `exclude` list. Parts never replace a block of the same name that was defined explicitly.
namespace QcPartGenerator {

// Why a block cannot be the base of building parts, or empty when it can.
QString unsuitableBase(const QcContentRegistry &registry, const QString &block);

// Generates all parts; returns warnings about materials that could not be used.
QStringList generate(QcContentRegistry &registry);

} // namespace QcPartGenerator
