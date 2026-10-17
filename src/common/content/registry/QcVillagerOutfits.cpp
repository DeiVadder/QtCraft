#include "common/content/registry/QcVillagerOutfits.h"

#include "common/content/registry/QcContentRegistry.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace {

bool isKind(const QcContentRegistry &registry, const QString &name, QcAppearanceDefinition::Kind kind)
{
    const QcAppearanceDefinition *appearance = registry.appearance(name);
    return appearance && appearance->kind == kind;
}

// Checks the names of a look; `owner` names it in the warnings.
void checkLook(const QcContentRegistry &registry, const QcVillagerLook &look, const QString &owner, QStringList &warnings)
{
    if (!isKind(registry, look.outfit, QcAppearanceDefinition::Kind::Outfit)) {
        warnings.append(u"villagers of %1: '%2' is no outfit"_s.arg(owner, look.outfit));
    }
    for (const QString &hair : look.hair) {
        if (!hair.isEmpty() && !isKind(registry, hair, QcAppearanceDefinition::Kind::Hair)) {
            warnings.append(u"villagers of %1: '%2' is no hair style"_s.arg(owner, hair));
        }
    }
}

// The outfit `base` with the layers of `profession` in place of its own of the same slot.
QcAppearanceDefinition joined(const QcAppearanceDefinition &base, const QcVillagerProfession &profession)
{
    QcAppearanceDefinition outfit = base;
    outfit.name = QcVillagerOutfits::outfitName(base.name, profession);
    outfit.playable = false;
    for (const QcAppearanceLayer &layer : profession.layers) {
        outfit.layers.removeIf([&layer](const QcAppearanceLayer &own) { return own.slot == layer.slot; });
        outfit.layers.append(layer);
    }
    return outfit;
}

} // namespace

namespace QcVillagerOutfits {

QString outfitName(const QString &outfit, const QcVillagerProfession &profession)
{
    return profession.layers.isEmpty() ? outfit : outfit + u'@' + profession.name;
}

QStringList generate(QcContentRegistry &registry)
{
    QStringList warnings;
    if (registry.villagerSpecies().isEmpty()) {
        return warnings;
    }

    QList<QPair<QString, QcVillagerLook>> looks;
    for (const QcVillagerSettings &species : std::as_const(registry).villagerSpecies()) {
        looks.append({u"villager settings '%1'"_s.arg(species.name), species.look});
    }
    for (const QcVillageStyleDefinition &style : std::as_const(registry.villageStyles())) {
        if (style.look.isSet()) {
            looks.append({u"style '%1'"_s.arg(style.name), style.look});
        }
    }
    for (const QcVillagerProfession &profession : registry.professions()) {
        if (!profession.hat.isEmpty() && !isKind(registry, profession.hat, QcAppearanceDefinition::Kind::Hair)) {
            warnings.append(u"profession '%1': hat '%2' is no hair style"_s.arg(profession.name, profession.hat));
        }
    }

    for (const auto &[owner, look] : std::as_const(looks)) {
        checkLook(registry, look, owner, warnings);
        const QcAppearanceDefinition *base = registry.appearance(look.outfit);
        if (!base || base->kind != QcAppearanceDefinition::Kind::Outfit) {
            continue;
        }
        const QcAppearanceDefinition outfit = *base;
        for (const QcVillagerProfession &profession : registry.professions()) {
            if (!profession.layers.isEmpty() && !registry.appearance(outfitName(outfit.name, profession))) {
                registry.registerAppearance(joined(outfit, profession));
            }
        }
    }
    return warnings;
}

} // namespace QcVillagerOutfits
