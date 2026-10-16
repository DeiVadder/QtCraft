#include "QcVillagerLooks.h"

#include "QcContentRegistry.h"
#include "QcVillager.h"
#include "QcVillagerOutfits.h"

namespace {

// One of `choices` by the bits of `seed` from `shift` on; `fallback` for none.
template<typename T>
T pick(const QList<T> &choices, const QList<T> &defaults, quint32 seed, int shift, const T &fallback)
{
    const QList<T> &list = choices.isEmpty() ? defaults : choices;
    return list.isEmpty() ? fallback : list[qsizetype(((seed >> shift) & 0xffu) % quint32(list.size()))];
}

const QcVillagerLook &lookOf(const QcContentRegistry &content, const QString &style)
{
    for (const QcVillageStyleDefinition &definition : content.villageStyles()) {
        if (definition.name == style && definition.villagers.isSet()) {
            return definition.villagers;
        }
    }
    return content.villagerSettings().look;
}

} // namespace

namespace QcVillagerLooks {

QcAppearance appearanceOf(const QcContentRegistry &content, const QcVillagerRecord &villager)
{
    const QcVillagerSettings &settings = content.villagerSettings();
    const QcVillagerLook &look = lookOf(content, villager.style);
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
