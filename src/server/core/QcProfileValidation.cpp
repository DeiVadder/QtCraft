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

// Letters, digits and symbols (emoji) name somebody; spaces and punctuation alone do not.
bool namesSomebody(char32_t codePoint)
{
    const QChar::Category category = QChar::category(codePoint);
    return QChar::isLetterOrNumber(codePoint) || category == QChar::Symbol_Math || category == QChar::Symbol_Currency
           || category == QChar::Symbol_Modifier || category == QChar::Symbol_Other;
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

bool hasMarkup(const QString &name)
{
    return std::ranges::any_of(markupCharacters, [&name](QChar character) { return name.contains(character); });
}

[[nodiscard]] std::optional<QString> validPlayerName(const QString &requested)
{
    QString name = QcNameText::clean(requested, maxPlayerNameLength);
    const QList<uint> codePoints = name.toUcs4();
    if (hasMarkup(name) || std::ranges::none_of(codePoints, [](uint codePoint) { return namesSomebody(char32_t(codePoint)); })) {
        return std::nullopt;
    }
    return name;
}

QString playerName(const QString &requested)
{
    return validPlayerName(requested).value_or(u"Player"_s);
}

} // namespace QcProfileValidation
