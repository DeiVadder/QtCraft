// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

class QcContentRegistry;
struct QcVillagerLook;
struct QcVillagerProfession;

// The outfits villagers wear (decision E-411): for every look (the default one of each villager
// species and each style's) and every profession with layers, the look's outfit with the profession's
// layers laid over it (an apron over the style's shirt), registered as a creature outfit under
// outfitName(). It runs when the content is finished (QcContentRegistry::finish), so the styles and
// professions of mods get theirs too, and the clients receive them with the other appearances.
namespace QcVillagerOutfits {

// The outfit a villager of `profession` wears in a look whose outfit is `outfit`: the joined one
// when the profession has layers, else the look's own.
QString outfitName(const QString &outfit, const QcVillagerProfession &profession);
// Registers the joined outfits; warns about looks whose outfit, hair or hats the content lacks.
QStringList generate(QcContentRegistry &registry);

} // namespace QcVillagerOutfits
