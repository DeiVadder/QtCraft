#include "QcProfileValidation.h"

#include "QcContentRegistry.h"
#include "QcNameText.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcProfileValidation {

namespace {

bool isPlayable(const QcAppearanceDefinition *definition, QcAppearanceDefinition::Kind kind)
{
    return definition && definition->kind == kind && definition->playable;
}

} // namespace

QcAppearance appearance(const QcContentRegistry &content, const QcAppearance &requested)
{
    QcAppearance result = requested;
    if (!isPlayable(content.appearance(requested.outfit), QcAppearanceDefinition::Kind::Outfit)) {
        // The default outfit, or else any outfit the content offers.
        result.outfit = QcAppearance().outfit;
        if (!isPlayable(content.appearance(result.outfit), QcAppearanceDefinition::Kind::Outfit)) {
            const auto any = std::ranges::find_if(content.appearances(), [](const QcAppearanceDefinition &definition) {
                return isPlayable(&definition, QcAppearanceDefinition::Kind::Outfit);
            });
            result.outfit = any != content.appearances().end() ? any->name : QString();
        }
    }

    if (!isPlayable(content.appearance(requested.hair), QcAppearanceDefinition::Kind::Hair)) {
        result.hair.clear();
    }
    if (!isPlayable(content.appearance(requested.cape), QcAppearanceDefinition::Kind::Cape)) {
        result.cape.clear();
    }

    for (quint32 *color : {&result.skinColor, &result.hairColor, &result.shirtColor, &result.pantsColor}) {
        *color &= 0xFFFFFFu;
    }
    return result;
}

QString playerName(const QString &requested)
{
    const QString name = QcNameText::clean(requested, maxPlayerNameLength);
    return name.isEmpty() ? u"Player"_s : name;
}

} // namespace QcProfileValidation
