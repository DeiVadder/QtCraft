// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/villages/QcVillagerLooks.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcVillagerOutfits.h"
#include "server/villages/QcVillager.h"

namespace {

// One of `choices` by the bits of `seed` from `shift` on; `fallback` for none.
template<typename T>
T pick(const QList<T> &choices, const QList<T> &defaults, quint32 seed, int shift, const T &fallback)
{
    const QList<T> &list = choices.isEmpty() ? defaults : choices;
    return list.isEmpty() ? fallback : list[static_cast<qsizetype>(((seed >> shift) & 0xffu) % static_cast<quint32>(list.size()))];
}

const QcVillagerLook &lookOf(const QcContentRegistry &content, const QcVillagerSettings &species, const QString &style)
{
    const QcVillageStyleDefinition *definition = content.villageCatalog().style(style);
    return definition && definition->look.isSet() ? definition->look : species.look;
}

} // namespace

namespace QcVillagerLooks {

QcAppearance appearanceOf(const QcContentRegistry &content, int mobType, const QcVillagerRecord &villager)
{
    const QcVillagerSettings &settings = content.villagerSettingsOf(mobType);
    const QcVillagerLook &look = lookOf(content, settings, villager.style);
    const QcVillagerLook &defaults = settings.look;
    const QcVillagerProfession *profession = settings.profession(villager.profession);

    QcAppearance appearance;
    appearance.outfit = profession ? QcVillagerOutfits::outfitName(look.outfit, *profession) : look.outfit;
    appearance.hair = profession && !profession->hat.isEmpty() ? profession->hat : pick(look.hair, defaults.hair, villager.look, 0, QString());
    appearance.skinColor = pick(look.skin, defaults.skin, villager.look, 8, appearance.skinColor);
    appearance.hairColor = pick(look.hairColor, defaults.hairColor, villager.look, 16, appearance.hairColor);
    appearance.shirtColor = pick(look.shirt, defaults.shirt, villager.look, 24, appearance.shirtColor);
    // The trousers take the bits of the hair and the skin turned, so they vary on their own.
    appearance.pantsColor = pick(look.pants, defaults.pants, (villager.look >> 4) | (villager.look << 28), 8, appearance.pantsColor);
    appearance.cape.clear();
    return appearance;
}

} // namespace QcVillagerLooks
